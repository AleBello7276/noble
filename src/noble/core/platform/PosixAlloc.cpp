#include "PosixAlloc.h"
#include <cstdlib>
#include <sys/mman.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/syscall.h>
#endif

PosixAlloc::~PosixAlloc() {
    if (mappedBase_)
        Release(mappedBase_, mappedSize_);
}

void* PosixAlloc::ReserveAliased(void* base, size_t size, std::span<const HostMemoryView> views) {
    if (mappedBase_ || !ValidViews(size, views))
        return nullptr;
    const auto pageSize = sysconf(_SC_PAGESIZE);
    if (pageSize <= 0)
        return nullptr;
    for (const auto& view : views)
        if (view.addressOffset % pageSize || view.backingOffset % pageSize || view.size % pageSize)
            return nullptr;
    std::vector<HostMemoryView> savedViews(views.begin(), views.end());
#if defined(__linux__)
    const int mapping = static_cast<int>(syscall(SYS_memfd_create, "noble-memory", 1u));
#else
    char path[] = "/tmp/noble-memory-XXXXXX";
    const int mapping = mkstemp(path);
    if (mapping != -1)
        unlink(path);
#endif
    if (mapping == -1)
        return nullptr;
    if (ftruncate(mapping, static_cast<off_t>(size)) != 0) {
        close(mapping);
        return nullptr;
    }
    auto* candidate = static_cast<uint8_t*>(Reserve(base, size));
    if (!candidate) {
        close(mapping);
        return nullptr;
    }
    for (const auto& view : views) {
        if (mmap(candidate + view.addressOffset, view.size, PROT_NONE, MAP_SHARED | MAP_FIXED, mapping,
                 static_cast<off_t>(view.backingOffset))
            == MAP_FAILED) {
            munmap(candidate, size);
            close(mapping);
            return nullptr;
        }
    }
    mapping_ = mapping;
    mappedBase_ = candidate;
    mappedSize_ = size;
    views_ = std::move(savedViews);
    return candidate;
}

void* PosixAlloc::Reserve(void* base, size_t size) {
    // A hint may be ignored by mmap; the caller uses the returned address.
    void* result = mmap(base, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return result == MAP_FAILED ? nullptr : result;
}

void* PosixAlloc::CommitRegion(void* ptr, size_t size) {
    if (IsAliased(ptr))
        return ProtectRegion(ptr, size, MemoryProtection::ReadWrite) ? ptr : nullptr;
    return mprotect(ptr, size, PROT_READ | PROT_WRITE) == 0 ? ptr : nullptr;
}

void* PosixAlloc::Allocate(size_t size) {
    void* result = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return result == MAP_FAILED ? nullptr : result;
}

bool PosixAlloc::DecommitRegion(void* ptr, size_t size) {
    if (IsAliased(ptr))
        return ProtectRegion(ptr, size, MemoryProtection::NoAccess);
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
    const auto protect
        = [flags](void* address, size_t bytes) { return mprotect(address, bytes, flags) == 0; };
    return IsAliased(ptr) ? ForEachAlias(ptr, size, protect) : protect(ptr, size);
}

void PosixAlloc::Release(void* ptr, size_t size) {
    if (mappedBase_ && ptr == mappedBase_) {
        munmap(mappedBase_, mappedSize_);
        close(mapping_);
        mapping_ = -1;
        mappedBase_ = nullptr;
        mappedSize_ = 0;
        views_.clear();
        return;
    }
    if (ptr)
        munmap(ptr, size);
}
