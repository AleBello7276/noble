#include "WinAlloc.h"

WinAlloc::~WinAlloc() {
    if (mappedBase_)
        Release(mappedBase_, mappedSize_);
}

void* WinAlloc::ReserveAliased(void* base, size_t size, std::span<const HostMemoryView> views) {
    if (mappedBase_ || !ValidViews(size, views))
        return nullptr;
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    for (const auto& view : views)
        if (view.addressOffset % info.dwAllocationGranularity
            || view.backingOffset % info.dwAllocationGranularity || view.size % info.dwPageSize)
            return nullptr;

    std::vector<HostMemoryView> savedViews(views.begin(), views.end());
    const auto mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE | SEC_RESERVE,
                                            DWORD(uint64_t(size) >> 32), DWORD(size), nullptr);
    if (!mapping)
        return nullptr;

    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        auto* candidate = static_cast<uint8_t*>(Reserve(attempt == 0 ? base : nullptr, size));
        if (!candidate)
            continue;

        VirtualFree(candidate, 0, MEM_RELEASE);
        size_t mapped = 0;
        for (const auto& view : views) {
            const auto address = MapViewOfFileEx(
                mapping, FILE_MAP_READ | FILE_MAP_WRITE, DWORD(uint64_t(view.backingOffset) >> 32),
                DWORD(view.backingOffset), view.size, candidate + view.addressOffset);
            if (!address)
                break;
            ++mapped;
        }

        if (mapped == views.size()) {
            mapping_ = mapping;
            mappedBase_ = candidate;
            mappedSize_ = size;
            views_ = std::move(savedViews);
            return candidate;
        }

        for (size_t i = 0; i < mapped; ++i)
            UnmapViewOfFile(candidate + views[i].addressOffset);
    }
    CloseHandle(mapping);
    return nullptr;
}

void* WinAlloc::Reserve(void* base, size_t size) {
    return VirtualAlloc(base, size, MEM_RESERVE, PAGE_NOACCESS);
}

void* WinAlloc::CommitRegion(void* ptr, size_t size) {
    if (IsAliased(ptr)) {
        const bool committed = ForEachAlias(ptr, size, [](void* address, size_t bytes) {
            return VirtualAlloc(address, bytes, MEM_COMMIT, PAGE_READWRITE) != nullptr;
        });

        if (!committed) {
            ProtectRegion(ptr, size, MemoryProtection::NoAccess);
            return nullptr;
        }

        return ProtectRegion(ptr, size, MemoryProtection::ReadWrite) ? ptr : nullptr;
    }

    return VirtualAlloc(ptr, size, MEM_COMMIT, PAGE_READWRITE);
}

void* WinAlloc::Allocate(size_t size) {
    return VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
}

bool WinAlloc::DecommitRegion(void* ptr, size_t size) {
    // windows cannot decommit SEC_RESERVE section pages so retire access in every view
    if (IsAliased(ptr))
        return ProtectRegion(ptr, size, MemoryProtection::NoAccess);

    return VirtualFree(ptr, size, MEM_DECOMMIT) != 0;
}

bool WinAlloc::ProtectRegion(void* ptr, size_t size, MemoryProtection protection) {
    DWORD flags = PAGE_NOACCESS;

    if (protection == MemoryProtection::ReadOnly)
        flags = PAGE_READONLY;
    else if (protection == MemoryProtection::ReadWrite)
        flags = PAGE_READWRITE;

    const auto protect = [flags](void* address, size_t bytes) {
        DWORD previous;
        return VirtualProtect(address, bytes, flags, &previous) != 0;
    };

    return IsAliased(ptr) ? ForEachAlias(ptr, size, protect) : protect(ptr, size);
}

void WinAlloc::Release(void* ptr, size_t) {
    if (mappedBase_ && ptr == mappedBase_) {
        for (const auto& view : views_)
            UnmapViewOfFile(mappedBase_ + view.addressOffset);
        CloseHandle(mapping_);
        mapping_ = nullptr;
        mappedBase_ = nullptr;
        mappedSize_ = 0;
        views_.clear();
        return;
    }

    if (ptr)
        VirtualFree(ptr, 0, MEM_RELEASE);
}
