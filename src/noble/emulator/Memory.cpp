
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
        return address - 0xE0000000;

    return address;
}

bool Memory::Initialise() {
    std::scoped_lock lock(mutex_);
    if (mMemoryBase_)
        return true;

    // try the preferred base first
    constexpr uintptr_t preferred = 0x100000000ull;
    mMemoryBase_
        = static_cast<uint8_t*>(Hostallocator_->Reserve(reinterpret_cast<void*>(preferred), kXboxMemorySize));

    if (!mMemoryBase_)
        mMemoryBase_ = static_cast<uint8_t*>(Hostallocator_->Reserve(nullptr, kXboxMemorySize));

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

    if (!Hostallocator_->CommitRegion(mMemoryBase_ + backing, size))
        return false;

    allocations_.emplace(backing, Allocation{size, static_cast<uint32_t>(address)});
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

    const uint64_t backing_begin = BackingAddress(heap.begin);
    const uint64_t backing_end = backing_begin + (heap.end - heap.begin);

    // keep the zero page free and search gaps in address order
    uint64_t candidate = AlignUp(backing_begin == 0 ? heap.page_size : backing_begin, alignment);

    for (auto it = allocations_.lower_bound(backing_begin);
         it != allocations_.end() && it->first < backing_end; ++it) {
        if (candidate + rounded <= it->first)
            break;
        candidate = AlignUp(std::max(candidate, it->first + it->second.size), alignment);
    }

    if (candidate + rounded > backing_end)
        return 0;

    const uint64_t guest = heap.begin + candidate - backing_begin;
    if (!AllocateAtLocked(guest, rounded))
        return 0;

    return static_cast<GuestAddress>(guest);
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

bool Memory::FreeVirtual(GuestAddress address) {
    std::scoped_lock lock(mutex_);

    // only the original guest address may release an aliased physical allocation
    auto it = allocations_.find(BackingAddress(address));

    if (it == allocations_.end() || it->second.guest_address != address)
        return false;

    if (!Hostallocator_->DecommitRegion(mMemoryBase_ + it->first, it->second.size))
        return false;

    allocations_.erase(it);
    return true;
}

bool Memory::IsMapped(GuestAddress address, size_t size) const {
    std::scoped_lock lock(mutex_);

    if (!mMemoryBase_ || !size || address == 0 || uint64_t(address) + size > kXboxMemorySize)
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
    return backing >= it->first && backing + size <= it->first + it->second.size;
}

void* Memory::Translate(uint32_t address, size_t size) const {
    return IsMapped(address, size) ? mMemoryBase_ + BackingAddress(address) : nullptr;
}
