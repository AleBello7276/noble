#pragma once
#include "core/HostAlloc.h"

class PosixAlloc final : public HostAlloc {
public:
    PosixAlloc() = default;
    ~PosixAlloc() override;
    PosixAlloc(const PosixAlloc&) = delete;
    PosixAlloc& operator=(const PosixAlloc&) = delete;
    void* Reserve(void* base, size_t size) override;
    void* ReserveAliased(void* base, size_t size, std::span<const HostMemoryView> views) override;
    void* CommitRegion(void* ptr, size_t size) override;
    void* Allocate(size_t size) override;
    bool DecommitRegion(void* ptr, size_t size) override;
    bool ProtectRegion(void* ptr, size_t size, MemoryProtection protection) override;
    void Release(void* ptr, size_t size) override;

private:
    int mapping_ = -1;
};
