#include "WinAlloc.h"

#include <assert.h>

void* WinAlloc::Reserve(void* base, size_t size) {
    void* ptr;

    ptr = VirtualAlloc(base, size, MEM_RESERVE, PAGE_READWRITE);
    assert(ptr);

    return ptr;
}

void* WinAlloc::CommitRegion(void* ptr, size_t size) {
    void* out;

    out = VirtualAlloc(ptr, size, MEM_COMMIT, PAGE_READWRITE);
    assert(out);

    return out;
}

void* WinAlloc::Allocate(size_t size) {
    void* out;

    out = VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    assert(out);

    return out;
}
