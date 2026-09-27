#pragma once

#include "core/HostAlloc.h"
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>

constexpr uint64_t AlignUp(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

/* guest address ranges and their allocation page sizes */
enum class GuestHeapKind { Virtual4K, Virtual64K, Image64K, Image4K, Physical64K, Physical16M, Physical4K };

using GuestAddress = uint32_t;

/* own the guest's 32-bit address space and commit host pages for guest allocations */
class Memory {
public:
    static constexpr uint64_t kXboxMemorySize = 0x100000000ull;

    Memory();
    ~Memory();
    Memory(const Memory&) = delete;
    Memory& operator=(const Memory&) = delete;

    /* reserve the 4 GB host address range used to back guest memory */
    bool Initialise();

    /* get the host reservation base, physical aliases must use Translate */
    void* GetMemoryBase() const { return mMemoryBase_; }

    /* allocate guest pages in the selected heap; return 0 on failure */
    GuestAddress AllocateVirtual(size_t size, size_t alignment = 4096,
                                 GuestHeapKind kind = GuestHeapKind::Virtual4K);

    /* allocate guest pages at a specific address */
    bool AllocateFixed(GuestAddress address, size_t size);

    /* release an allocation using the address returned when it was created */
    bool FreeVirtual(GuestAddress address);

    /* check whether the full guest address range has backing pages */
    bool IsMapped(GuestAddress address, size_t size = 1) const;

    /* translate a mapped guest range to its host backing address */
    void* Translate(GuestAddress address, size_t size = 1) const;

    /* typed version of Translate for guest pointers */
    template <typename T = uint8_t*>
    T GuestToHostVirtual(uint32_t address) const {
        return reinterpret_cast<T>(Translate(address));
    }

private:
    /* half open guest range and its allocation page size */
    struct Heap {
        uint64_t begin, end;
        size_t page_size;
    };

    /* backing size and the guest address that owns the allocation */
    struct Allocation {
        uint64_t size;
        uint32_t guest_address;
    };

    static const Heap& HeapFor(GuestHeapKind kind);
    static const Heap* HeapAt(uint64_t address);

    /* map each physical address window to the same 512 MB backing range */
    static uint64_t BackingAddress(uint64_t address);

    /* commit a range while mutex_ is held */
    bool AllocateAtLocked(uint64_t address, uint64_t size);

    uint8_t* mMemoryBase_ = nullptr;
    std::unique_ptr<HostAlloc> Hostallocator_;
    mutable std::mutex mutex_;
    std::map<uint64_t, Allocation> allocations_;
};
