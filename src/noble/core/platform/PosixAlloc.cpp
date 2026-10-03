#include "PosixAlloc.h"
#include <sys/mman.h>

void* PosixAlloc::Reserve(void* base, size_t size) {
    // A hint may be ignored by mmap; the caller uses the returned address.
    void* result = mmap(base, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return result == MAP_FAILED ? nullptr : result;
}

void* PosixAlloc::CommitRegion(void* ptr, size_t size) {
    return mprotect(ptr, size, PROT_READ | PROT_WRITE) == 0 ? ptr : nullptr;
}

void* PosixAlloc::Allocate(size_t size) {
    void* result = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return result == MAP_FAILED ? nullptr : result;
}

bool PosixAlloc::DecommitRegion(void* ptr, size_t size) {
    if (mprotect(ptr, size, PROT_NONE) != 0)
        return false;
    madvise(ptr, size, MADV_DONTNEED);
    return true;
}

bool PosixAlloc::ProtectRegion(void* ptr, size_t size, MemoryProtection protection) {
    int flags = PROT_NONE;
    if (protection == MemoryProtection::ReadOnly)
        flags = PROT_READ;
    else if (protection == MemoryProtection::ReadWrite)
        flags = PROT_READ | PROT_WRITE;
    return mprotect(ptr, size, flags) == 0;
}

void PosixAlloc::Release(void* ptr, size_t size) {
    if (ptr)
        munmap(ptr, size);
}
