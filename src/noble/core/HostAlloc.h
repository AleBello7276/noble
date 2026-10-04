#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

enum class MemoryProtection { NoAccess, ReadOnly, ReadWrite };

// a view of shared storage at an offset within the reserved host address range
struct HostMemoryView {
    size_t addressOffset;
    size_t backingOffset;
    size_t size;
};

class HostAlloc {
public:
    virtual ~HostAlloc() = default;

    /* reserve a region of Host memory*/
    virtual void* Reserve(void* base, size_t size) = 0;

    // map shared storage into contiguous views with initially inaccessible pages
    virtual void* ReserveAliased(void* base, size_t size, std::span<const HostMemoryView> views) = 0;

    /* commit a region of Host memory previously reserved */
    virtual void* CommitRegion(void* ptr, size_t size) = 0;

    /* allocate `size` memory */
    virtual void* Allocate(size_t size) = 0;
    virtual bool DecommitRegion(void* ptr, size_t size) = 0;

    // change access permissions on committed host pages
    virtual bool ProtectRegion(void* ptr, size_t size, MemoryProtection protection) = 0;
    virtual void Release(void* ptr, size_t size) = 0;

protected:
    static bool ValidViews(size_t size, std::span<const HostMemoryView> views) {
        size_t end = 0;
        for (const auto& view : views) {
            if (!view.size || view.addressOffset != end || view.size > size - end || view.backingOffset > size
                || view.size > size - view.backingOffset)
                return false;
            end += view.size;
        }
        return end == size;
    }

    bool IsAliased(const void* ptr) const {
        const auto address = reinterpret_cast<uintptr_t>(ptr);
        const auto base = reinterpret_cast<uintptr_t>(mappedBase_);
        return mappedBase_ && address >= base && address - base < mappedSize_;
    }

    // apply a page operation to every view of the same backing bytes
    template <typename Operation>
    bool ForEachAlias(void* ptr, size_t size, Operation operation) {
        if (!IsAliased(ptr) || !size)
            return false;
        const auto offset = reinterpret_cast<uintptr_t>(ptr) - reinterpret_cast<uintptr_t>(mappedBase_);
        for (const auto& source : views_) {
            if (offset < source.addressOffset || offset - source.addressOffset >= source.size)
                continue;
            if (size > source.size - (offset - source.addressOffset))
                return false;
            const auto begin = source.backingOffset + offset - source.addressOffset;
            const auto end = begin + size;
            for (const auto& view : views_) {
                const auto overlapBegin = (std::max)(begin, view.backingOffset);
                const auto overlapEnd = (std::min)(end, view.backingOffset + view.size);
                if (overlapBegin < overlapEnd
                    && !operation(mappedBase_ + view.addressOffset + overlapBegin - view.backingOffset,
                                  overlapEnd - overlapBegin))
                    return false;
            }
            return true;
        }
        return false;
    }

    uint8_t* mappedBase_ = nullptr;
    size_t mappedSize_ = 0;
    std::vector<HostMemoryView> views_;
};
