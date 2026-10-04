#include "Memory.h"

#include <array>
#include <bit>
#include <new>

namespace hle::krnl {

constexpr uint32_t statusSuccess = 0;
constexpr uint32_t statusNotImplemented = 0xC0000002;
constexpr uint32_t statusAccessViolation = 0xC0000005;
constexpr uint32_t statusInvalidParameter = 0xC000000D;
constexpr uint32_t statusNoMemory = 0xC0000017;
constexpr uint32_t statusConflictingAddresses = 0xC0000018;
constexpr uint32_t statusInvalidPageProtection = 0xC0000045;

constexpr uint32_t memCommit = 0x1000;
constexpr uint32_t memReserve = 0x2000;
constexpr uint32_t memReset = 0x80000;
constexpr uint32_t memTopDown = 0x100000;
constexpr uint32_t memNoZero = 0x800000;
constexpr uint32_t memLargePages = 0x20000000;
constexpr uint32_t memHeap = 0x40000000;
constexpr uint32_t mem16MBPages = 0x80000000;

// allocate guest virtual pages and update the caller's big endian base and size on success
uint32_t NtAllocateVirtualMemory(Memory& memory, Pointer<be<uint32_t>, PointerValidation::Report> baseAddress,
                                 Pointer<be<uint32_t>, PointerValidation::Report> regionSize,
                                 uint32_t allocationType, uint32_t protect, uint32_t debugMemory) {
    if (!baseAddress.guest_address() || !regionSize.guest_address())
        return statusInvalidParameter;

    if (!baseAddress || !regionSize)
        return statusAccessViolation;

    const uint64_t basePointer = baseAddress.guest_address();
    const uint64_t sizePointer = regionSize.guest_address();

    if (basePointer < sizePointer + 4 && sizePointer < basePointer + 4)
        return statusInvalidParameter;

    const GuestAddress base = *baseAddress;
    const uint32_t requestedSize = *regionSize;

    constexpr uint32_t supportedFlags
        = memCommit | memReserve | memReset | memTopDown | memNoZero | memLargePages | memHeap | mem16MBPages;

    if (!requestedSize || (allocationType & ~supportedFlags)
        || !(allocationType & (memCommit | memReserve | memReset)))
        return statusInvalidParameter;

    if ((allocationType & memReset) && allocationType != memReset)
        return statusInvalidParameter;

    if (allocationType & (memReset | mem16MBPages))
        return statusNotImplemented;

    // cache modifiers do not change host access and executable guest pages remain ordinary guest data
    const uint32_t access = protect & 0xFF;

    if (!std::has_single_bit(access) || (protect & ~0x7FFu) || (protect & 0x600) == 0x600)
        return statusInvalidPageProtection;

    if (protect & 0x100)
        return statusNotImplemented;

    const MemoryProtection protection = access == 1 ? MemoryProtection::NoAccess :
                                        (access == 2 || access == 0x10 || access == 0x20) ?
                                                      MemoryProtection::ReadOnly :
                                                      MemoryProtection::ReadWrite;

    const GuestHeapKind kind
        = allocationType & memLargePages ? GuestHeapKind::Virtual64K : GuestHeapKind::Virtual4K;

    const uint64_t pageSize = base                              ? Memory::VirtualPageSize(base) :
                              kind == GuestHeapKind::Virtual64K ? 65536 :
                                                                  4096;
    if (!pageSize)
        return statusInvalidParameter;

    const GuestAddress adjustedBase = static_cast<GuestAddress>(uint64_t(base) / pageSize * pageSize);

    if (base && !adjustedBase)
        return statusInvalidParameter;

    // some titles pass a negative signed size and anonymous allocations are rounded to 64 kb
    const uint64_t magnitude
        = requestedSize & 0x80000000u ? uint64_t(0x100000000ull - requestedSize) : uint64_t(requestedSize);

    const uint64_t size = AlignUp(magnitude, base ? pageSize : 65536);
    if (size > UINT32_MAX || (base && uint64_t(adjustedBase) + size > Memory::kXboxMemorySize))
        return statusNoMemory;

    // devkit requests share normal guest memory and fresh pages are always zeroed even with memNoZero
    (void)debugMemory;
    GuestAddress address = 0;
    VirtualAllocationResult result;

    try {
        result = memory.AllocateVirtualRegion(adjustedBase, static_cast<size_t>(size), kind,
                                              allocationType & memReserve, allocationType & memCommit,
                                              allocationType & memTopDown, protection, address);

    } catch (const std::bad_alloc&) {
        return statusNoMemory;
    }

    switch (result) {
    case VirtualAllocationResult::InvalidAddress:
        return statusInvalidParameter;

    case VirtualAllocationResult::Conflict:
        return statusConflictingAddresses;

    case VirtualAllocationResult::NoMemory:
        return statusNoMemory;

    case VirtualAllocationResult::Success:
        *baseAddress = address;
        *regionSize = static_cast<uint32_t>(size);
        return statusSuccess;
    }

    return statusNoMemory;
}

uint32_t MmAllocatePhysicalMemoryEx(Memory& memory, uint32_t flags, uint32_t regionSize, uint32_t protect,
                                    uint32_t minimum, uint32_t maximum, uint32_t alignment) {
    constexpr uint32_t supported = 0x2 | 0x4 | 0x200 | 0x400 | memLargePages | mem16MBPages;
    const uint32_t access = protect & 0xFF;
    if ((access != 0x2 && access != 0x4) || (protect & ~supported) || (protect & 0x600) == 0x600
        || (protect & (memLargePages | mem16MBPages)) == (memLargePages | mem16MBPages))
        return 0;

    const GuestHeapKind kind = protect & memLargePages ? GuestHeapKind::Physical64K :
                               protect & mem16MBPages  ? GuestHeapKind::Physical16M :
                                                         GuestHeapKind::Physical4K;
    const MemoryProtection protection
        = access == 0x2 ? MemoryProtection::ReadOnly : MemoryProtection::ReadWrite;
    try {
        return memory.AllocatePhysical(regionSize, alignment, kind, minimum, maximum, protection);
    } catch (const std::bad_alloc&) {
        return 0;
    }
}

constexpr std::array exports{
    Bind<&NtAllocateVirtualMemory>(XboxLibrary::XboxKrnl, "NtAllocateVirtualMemory"),
    Bind<&MmAllocatePhysicalMemoryEx>(XboxLibrary::XboxKrnl, "MmAllocatePhysicalMemoryEx"),
};

std::span<const Export> MemoryExports() {
    return exports;
}

}  // namespace hle::krnl
