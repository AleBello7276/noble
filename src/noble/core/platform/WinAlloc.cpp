#include "WinAlloc.h"

void* WinAlloc::Reserve(void* base, size_t size) {
    return VirtualAlloc(base, size, MEM_RESERVE, PAGE_NOACCESS);
}

void* WinAlloc::CommitRegion(void* ptr, size_t size) {
    return VirtualAlloc(ptr, size, MEM_COMMIT, PAGE_READWRITE);
}

void* WinAlloc::Allocate(size_t size) {
    return VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

bool WinAlloc::DecommitRegion(void* ptr, size_t size) {
    return VirtualFree(ptr, size, MEM_DECOMMIT) != 0;
}

bool WinAlloc::ProtectRegion(void* ptr, size_t size, MemoryProtection protection) {
    DWORD flags = PAGE_NOACCESS;

    if (protection == MemoryProtection::ReadOnly)
        flags = PAGE_READONLY;
    else if (protection == MemoryProtection::ReadWrite)
        flags = PAGE_READWRITE;

    DWORD previous;
    return VirtualProtect(ptr, size, flags, &previous) != 0;
}

void WinAlloc::Release(void* ptr, size_t) {
    if (ptr)
        VirtualFree(ptr, 0, MEM_RELEASE);
}
