
#include "Memory.h"

#include "Logger.h"

#ifdef _WIN32
#include "core/platform/WinAlloc.h"
#else
#include "core/platform/PosixAlloc.h"
#endif

#include <algorithm>
#include <array>
#include <cstring>

Memory::Memory()
#ifdef _WIN32
    : Hostallocator_(std::make_unique<WinAlloc>())
#else
    : Hostallocator_(std::make_unique<PosixAlloc>())
#endif
{
}

Memory::~Memory() {
    if (mMemoryBase_)
        Hostallocator_->Release(mMemoryBase_, kXboxMemorySize);
}

/* Xenia's guest ranges and page sizes, 0x7F000000-0x7FFFFFFF for GPU/MMIO */
const Memory::Heap& Memory::HeapFor(GuestHeapKind kind) {
    static constexpr std::array<Heap, 7> heaps{{
        {0x00000000, 0x40000000, 0x1000},
        {0x40000000, 0x7F000000, 0x10000},
        {0x80000000, 0x90000000, 0x10000},
        {0x90000000, 0xA0000000, 0x1000},
        {0xA0000000, 0xC0000000, 0x10000},
        {0xC0000000, 0xE0000000, 0x1000000},
        {0xE0000000, 0xFFD00000, 0x1000},
    }};
    return heaps[static_cast<size_t>(kind)];
}

const Memory::Heap* Memory::HeapAt(uint64_t address) {
    for (size_t i = 0; i < 7; ++i) {
        const auto& heap = HeapFor(static_cast<GuestHeapKind>(i));
        if (address >= heap.begin && address < heap.end)
            return &heap;
    }

    return nullptr;
}

/* The three physical windows alias the first 512 MB of guest backing storage */
uint64_t Memory::BackingAddress(uint64_t address) {
    if (address >= 0xA0000000 && address < 0xC0000000)
        return address - 0xA0000000;

    if (address >= 0xC0000000 && address < 0xE0000000)
        return address - 0xC0000000;

    if (address >= 0xE0000000 && address < 0xFFD00000)
        return address - 0xE0000000 + 0x1000;

    return address;
}

bool Memory::Initialise() {
    std::scoped_lock lock(mutex_);
    if (mMemoryBase_)
        return true;

    // physical windows share the low 512 mb backing through host page mappings
    static constexpr std::array<HostMemoryView, 5> views{{
        {0x00000000, 0x00000000, 0xA0000000},
        {0xA0000000, 0x00000000, 0x20000000},
        {0xC0000000, 0x00000000, 0x20000000},
        {0xE0000000, 0x00001000, 0x1FD00000},
        {0xFFD00000, 0xFFD00000, 0x00300000},
    }};
    constexpr uintptr_t preferred = 0x100000000ull;
    mMemoryBase_ = static_cast<uint8_t*>(
        Hostallocator_->ReserveAliased(reinterpret_cast<void*>(preferred), kXboxMemorySize, views));

    if (!mMemoryBase_) {
        LOG_FATAL("Unable to reserve guest address space");
        return false;
    }

    return true;
}

bool Memory::AllocateAtLocked(uint64_t address, uint64_t size) {
    const uint64_t backing = BackingAddress(address);
    if (!mMemoryBase_ || !size || backing + size > kXboxMemorySize)
        return false;

    // check backing addresses so physical aliases cannot claim the same pages twice
    auto next = allocations_.lower_bound(backing);
    if (next != allocations_.end() && next->first < backing + size)
        return false;

    if (next != allocations_.begin()) {
        auto previous = std::prev(next);
        if (previous->first + previous->second.size > backing)
            return false;
    }

    auto [allocation, inserted] = allocations_.emplace(
        backing, Allocation{size, static_cast<uint32_t>(address),
                            std::vector<uint8_t>(size / 4096, uint8_t(MemoryProtection::ReadWrite) + 1)});
    if (!inserted)
        return false;

    if (!Hostallocator_->CommitRegion(mMemoryBase_ + backing, size)) {
        allocations_.erase(allocation);
        return false;
    }

    std::memset(mMemoryBase_ + backing, 0, size);
    return true;
}

GuestAddress Memory::AllocateVirtual(size_t size, size_t alignment, GuestHeapKind kind) {
    const Heap& heap = HeapFor(kind);
    if (!size || size > heap.end - heap.begin || !alignment)
        return 0;

    alignment = std::max(alignment, heap.page_size);
    if (alignment % heap.page_size)
        return 0;

    const uint64_t rounded = AlignUp(size, heap.page_size);

    std::scoped_lock lock(mutex_);

    const uint64_t candidate = FindFreeLocked(heap, rounded, alignment, false);
    if (candidate == kXboxMemorySize)
        return 0;

    const uint64_t guest = heap.begin + candidate - BackingAddress(heap.begin);
    if (!AllocateAtLocked(guest, rounded))
        return 0;

    return static_cast<GuestAddress>(guest);
}

GuestAddress Memory::AllocatePhysical(size_t size, size_t alignment, GuestHeapKind kind, uint32_t minimum,
                                      uint32_t maximum, MemoryProtection protection) {
    if (kind != GuestHeapKind::Physical4K && kind != GuestHeapKind::Physical64K
        && kind != GuestHeapKind::Physical16M)
        return 0;

    const Heap& heap = HeapFor(kind);
    const uint64_t physicalSize = heap.end - heap.begin;
    const uint64_t physicalBegin = BackingAddress(heap.begin);
    const uint64_t physicalEnd = physicalBegin + physicalSize;
    if (!size || size > physicalSize || minimum > maximum || minimum >= physicalEnd
        || alignment > kXboxMemorySize)
        return 0;

    const uint64_t roundedSize = AlignUp(size, heap.page_size);
    const uint64_t roundedAlignment = AlignUp(std::max<uint64_t>(alignment, heap.page_size), heap.page_size);
    const uint64_t lower = std::max<uint64_t>(minimum, physicalBegin);
    const uint64_t upper = std::min<uint64_t>(uint64_t(maximum) + 1, physicalEnd);
    if (lower >= upper || roundedSize > upper - lower)
        return 0;

    const Heap range{heap.begin + lower - physicalBegin, heap.begin + upper - physicalBegin, heap.page_size};
    std::scoped_lock lock(mutex_);
    if (!mMemoryBase_)
        return 0;

    const uint64_t backing = FindFreeLocked(range, roundedSize, roundedAlignment, true);
    if (backing == kXboxMemorySize)
        return 0;

    const auto guest = static_cast<GuestAddress>(heap.begin + backing - physicalBegin);
    auto [allocation, inserted] = allocations_.emplace(
        backing, Allocation{roundedSize, guest, std::vector<uint8_t>(roundedSize / 4096)});
    if (!inserted)
        return 0;

    try {
        if (!CommitLocked(backing, allocation->second, 0, roundedSize, protection)) {
            allocations_.erase(allocation);
            return 0;
        }
    } catch (...) {
        allocations_.erase(allocation);
        throw;
    }

    return guest;
}

bool Memory::FreePhysical(GuestAddress address) {
    if (address < HeapFor(GuestHeapKind::Physical64K).begin
        || address >= HeapFor(GuestHeapKind::Physical4K).end)
        return false;

    return FreeVirtual(address);
}

bool Memory::AllocateFixed(GuestAddress address, size_t size) {
    const Heap* heap = HeapAt(address);

    if (!heap || !size || address % heap->page_size || size > heap->end - address)
        return false;

    const uint64_t rounded = AlignUp(size, heap->page_size);
    if (rounded > heap->end - address)
        return false;

    std::scoped_lock lock(mutex_);
    return AllocateAtLocked(address, rounded);
}

bool Memory::MapDeviceMemory(GuestAddress address, size_t size) {
    constexpr uint64_t begin = 0x7F000000;
    constexpr uint64_t end = 0x80000000;
    if (address < begin || address >= end || !size || address % 4096 || size % 4096 || size > end - address)
        return false;

    std::scoped_lock lock(mutex_);
    return AllocateAtLocked(address, size);
}

bool Memory::FreeVirtual(GuestAddress address) {
    std::scoped_lock lock(mutex_);

    // only the original guest address may release an aliased physical allocation
    auto it = allocations_.find(BackingAddress(address));

    if (it == allocations_.end() || it->second.guest_address != address)
        return false;

    if (!DecommitLocked(it->first, it->second, 0, it->second.size))
        return false;

    allocations_.erase(it);
    return true;
}

bool Memory::DecommitLocked(uint64_t backing, Allocation& allocation, uint64_t offset, uint64_t size) {
    const size_t end = (offset + size) / 4096;
    for (size_t page = offset / 4096; page < end;) {
        if (!allocation.pages[page]) {
            ++page;
            continue;
        }

        const size_t first = page++;
        while (page < end && allocation.pages[page])
            ++page;

        if (!Hostallocator_->DecommitRegion(mMemoryBase_ + backing + first * 4096, (page - first) * 4096))
            return false;

        std::fill_n(allocation.pages.begin() + first, page - first, uint8_t(0));
    }

    return true;
}

VirtualFreeResult Memory::FreeVirtualRegion(GuestAddress address, uint32_t size, bool release,
                                            GuestAddress& resultAddress, uint32_t& resultSize,
                                            std::span<const GuestAddress> preservedOutputs) {
    const auto pageSize = VirtualPageSize(address);
    if (!address)
        return VirtualFreeResult::NotAllocated;

    if (!pageSize)
        return VirtualFreeResult::InvalidAddress;

    std::scoped_lock lock(mutex_);
    auto it = allocations_.upper_bound(address);
    if (it == allocations_.begin())
        return VirtualFreeResult::NotAllocated;

    --it;
    auto& allocation = it->second;
    if (address - it->first >= allocation.size || allocation.guest_address != it->first)
        return VirtualFreeResult::NotAllocated;

    uint64_t base = uint64_t(address) / pageSize * pageSize;
    uint64_t bytes = 0;
    if (release || !size) {
        if (address != it->first)
            return VirtualFreeResult::InvalidAddress;

        base = it->first;
        bytes = allocation.size;
    } else {
        bytes = AlignUp(uint64_t(address) - base + size, pageSize);
        if (base < it->first || bytes > allocation.size - (base - it->first))
            return VirtualFreeResult::InvalidAddress;
    }

    for (const auto output : preservedOutputs)
        if (uint64_t(output) < base + bytes && uint64_t(output) + 4 > base)
            return VirtualFreeResult::InvalidAddress;

    if (!DecommitLocked(it->first, allocation, base - it->first, bytes))
        return VirtualFreeResult::Failed;

    if (release)
        allocations_.erase(it);

    resultAddress = static_cast<GuestAddress>(base);
    resultSize = static_cast<uint32_t>(bytes);
    return VirtualFreeResult::Success;
}

bool Memory::IsAccessibleLocked(GuestAddress address, size_t size, bool checkAccess, bool write) const {
    if (!mMemoryBase_ || !size || address == 0 || size > kXboxMemorySize - address)
        return false;

    const uint64_t backing = BackingAddress(address);
    const Heap* heap = HeapAt(address);

    // a range may not cross from one guest heap into another
    if (heap && uint64_t(address) + size > heap->end)
        return false;

    auto it = allocations_.upper_bound(backing);
    if (it == allocations_.begin())
        return false;

    --it;
    if (backing < it->first || backing - it->first >= it->second.size
        || size > it->second.size - (backing - it->first))
        return false;

    const size_t first = (backing - it->first) / 4096;
    const size_t last = (backing - it->first + size - 1) / 4096;

    for (size_t page = first; page <= last; ++page) {
        const uint8_t state = it->second.pages[page];

        if (!state || (checkAccess && state < (write ? 3 : 2)))
            return false;
    }
    return true;
}

bool Memory::IsMapped(GuestAddress address, size_t size) const {
    std::scoped_lock lock(mutex_);
    return IsAccessibleLocked(address, size, false, false);
}

bool Memory::IsAccessible(GuestAddress address, size_t size, bool write) const {
    std::scoped_lock lock(mutex_);
    return IsAccessibleLocked(address, size, true, write);
}

void* Memory::Translate(uint32_t address, size_t size) const {
    return IsAccessible(address, size) ? mMemoryBase_ + BackingAddress(address) : nullptr;
}

bool Memory::ReadBytes(GuestAddress address, std::span<std::byte> destination) const {
    std::lock_guard lock(mutex_);

    if (!IsAccessibleLocked(address, destination.size(), true, false))
        return false;

    if (!destination.empty())
        std::memcpy(destination.data(), mMemoryBase_ + BackingAddress(address), destination.size());

    return true;
}

uint32_t Memory::GetPhysicalAddress(GuestAddress address) const {
    const uint64_t backing = BackingAddress(address);
    return backing < 0x20000000 ? static_cast<uint32_t>(backing) : UINT32_MAX;
}

size_t Memory::VirtualPageSize(GuestAddress address) {
    if (address < 0x40000000 || (address >= 0x90000000 && address < 0xA0000000))
        return 4096;

    if ((address >= 0x40000000 && address < 0x7F000000) || (address >= 0x80000000 && address < 0x90000000))
        return 65536;

    return 0;
}

uint64_t Memory::FindFreeLocked(const Heap& heap, uint64_t size, uint64_t alignment, bool topDown) const {
    const uint64_t begin = BackingAddress(heap.begin);
    const uint64_t end = begin + heap.end - heap.begin;

    uint64_t cursor = heap.begin == 0 ? heap.page_size : begin;
    uint64_t selected = kXboxMemorySize;

    const auto gap = [&](uint64_t limit) {
        if (limit < cursor || size > limit - cursor)
            return;

        const uint64_t candidate
            = topDown ? (limit - size) / alignment * alignment : AlignUp(cursor, alignment);

        if (candidate >= cursor && candidate <= limit - size)
            selected = candidate;
    };

    auto it = allocations_.upper_bound(begin);
    if (it != allocations_.begin())
        --it;

    for (; it != allocations_.end() && it->first < end; ++it) {
        gap(it->first);

        if (!topDown && selected != kXboxMemorySize)
            return selected;

        cursor = std::max(cursor, it->first + it->second.size);
    }

    gap(end);
    return selected;
}

bool Memory::CommitLocked(uint64_t backing, Allocation& allocation, uint64_t offset, uint64_t size,
                          MemoryProtection protection) {
    const size_t first = offset / 4096;
    const size_t end = (offset + size) / 4096;

    std::vector<std::pair<size_t, size_t>> runs;
    for (size_t page = first; page < end;) {
        if (allocation.pages[page]) {
            ++page;
            continue;
        }

        const size_t start = page++;

        while (page < end && !allocation.pages[page])
            ++page;

        runs.emplace_back(start, page - start);
    }

    size_t completed = 0;
    bool success = true;

    for (const auto [page, count] : runs) {
        void* ptr = mMemoryBase_ + backing + page * 4096;
        const size_t bytes = count * 4096;

        if (!Hostallocator_->CommitRegion(ptr, bytes)) {
            success = false;
            break;
        }

        ++completed;
        std::memset(ptr, 0, bytes);

        if (!Hostallocator_->ProtectRegion(ptr, bytes, protection)) {
            success = false;
            break;
        }
    }

    if (!success) {
        for (size_t i = 0; i < completed; ++i)
            Hostallocator_->DecommitRegion(mMemoryBase_ + backing + runs[i].first * 4096,
                                           runs[i].second * 4096);

        return false;
    }

    for (const auto [page, count] : runs)
        std::fill_n(allocation.pages.begin() + page, count, uint8_t(protection) + 1);

    return true;
}

VirtualAllocationResult Memory::AllocateVirtualRegion(GuestAddress address, size_t size, GuestHeapKind kind,
                                                      bool reserve, bool commit, bool topDown,
                                                      MemoryProtection protection, GuestAddress& result) {
    const Heap* heap = address ? HeapAt(address) : &HeapFor(kind);

    if (!heap || !size || (!reserve && !commit) || heap->begin >= 0xA0000000
        || (kind != GuestHeapKind::Virtual4K && kind != GuestHeapKind::Virtual64K)
        || address % heap->page_size || size % heap->page_size)
        return VirtualAllocationResult::InvalidAddress;

    if (size > heap->end - heap->begin)
        return VirtualAllocationResult::NoMemory;

    if (address && (address < heap->page_size || size > heap->end - address))
        return VirtualAllocationResult::InvalidAddress;

    std::scoped_lock lock(mutex_);
    if (!mMemoryBase_)
        return VirtualAllocationResult::NoMemory;

    uint64_t backing = address ? uint64_t(address) : FindFreeLocked(*heap, size, heap->page_size, topDown);
    if (backing == kXboxMemorySize)
        return VirtualAllocationResult::NoMemory;

    auto next = allocations_.upper_bound(backing);
    if (next != allocations_.begin()) {
        auto previous = std::prev(next);

        if (backing < previous->first + previous->second.size) {
            if (reserve || size > previous->first + previous->second.size - backing)
                return VirtualAllocationResult::Conflict;

            if (!CommitLocked(previous->first, previous->second, backing - previous->first, size, protection))
                return VirtualAllocationResult::NoMemory;
            result = static_cast<GuestAddress>(backing);

            return VirtualAllocationResult::Success;
        }
    }

    if (next != allocations_.end() && next->first < backing + size)
        return VirtualAllocationResult::Conflict;

    if (address && !reserve)
        return VirtualAllocationResult::Conflict;

    auto [allocation, inserted] = allocations_.emplace(
        backing, Allocation{size, static_cast<GuestAddress>(backing), std::vector<uint8_t>(size / 4096)});

    if (!inserted)
        return VirtualAllocationResult::Conflict;

    try {
        if (commit && !CommitLocked(backing, allocation->second, 0, size, protection)) {
            allocations_.erase(allocation);
            return VirtualAllocationResult::NoMemory;
        }
    } catch (...) {
        allocations_.erase(allocation);
        throw;
    }

    result = static_cast<GuestAddress>(backing);

    return VirtualAllocationResult::Success;
}
