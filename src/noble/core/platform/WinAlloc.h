#pragma once

#include "core/HostAlloc.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

class WinAlloc : public HostAlloc {
public:
    WinAlloc() = default;

    void* Reserve(void* base, size_t size) override;

    void* CommitRegion(void* ptr, size_t size) override;

    void* Allocate(size_t size) override;
    bool DecommitRegion(void* ptr, size_t size) override;
    bool ProtectRegion(void* ptr, size_t size, MemoryProtection protection) override;
    void Release(void* ptr, size_t size) override;
};
