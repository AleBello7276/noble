#pragma once

#include "core/HostAlloc.h"

#include <Windows.h>

class WinAlloc : public HostAlloc {
public:
    WinAlloc() = default;

    void* Reserve(void* base, size_t size) override;

    void* CommitRegion(void* ptr, size_t size) override;

    void* Allocate(size_t size) override;
};
