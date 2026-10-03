#pragma once
#include <cstddef>

enum class MemoryProtection { NoAccess, ReadOnly, ReadWrite };

class HostAlloc {
public:
    virtual ~HostAlloc() = default;

    /* reserve a region of Host memory*/
    virtual void* Reserve(void* base, size_t size) = 0;

    /* commit a region of Host memory previously reserved */
    virtual void* CommitRegion(void* ptr, size_t size) = 0;

    /* allocate `size` memory */
    virtual void* Allocate(size_t size) = 0;
    virtual bool DecommitRegion(void* ptr, size_t size) = 0;

    // change access permissions on committed host pages
    virtual bool ProtectRegion(void* ptr, size_t size, MemoryProtection protection) = 0;
    virtual void Release(void* ptr, size_t size) = 0;
};
