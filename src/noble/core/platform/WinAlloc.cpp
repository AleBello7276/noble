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
        if (view.addressOffset % info.dwPageSize || view.backingOffset % info.dwPageSize
            || view.size % info.dwPageSize)
            return nullptr;

    std::vector<HostMemoryView> savedViews(views.begin(), views.end());
    const auto mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE | SEC_RESERVE,
                                            DWORD(uint64_t(size) >> 32), DWORD(size), nullptr);
    if (!mapping)
        return nullptr;

    const auto process = GetCurrentProcess();
    auto* candidate = static_cast<uint8_t*>(
        VirtualAlloc2(process, base, size, MEM_RESERVE | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS, nullptr, 0));
    if (!candidate && base)
        candidate = static_cast<uint8_t*>(VirtualAlloc2(
            process, nullptr, size, MEM_RESERVE | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS, nullptr, 0));
    if (!candidate) {
        CloseHandle(mapping);
        return nullptr;
    }

    size_t mapped = 0;
    bool split = false;
    for (const auto& view : views) {
        split = false;
        if (view.addressOffset + view.size < size) {
            // split the remaining reservation while retaining ownership of all its addresses
            if (!VirtualFree(candidate + view.addressOffset, view.size,
                             MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER))
                break;
            split = true;
        }
        if (!MapViewOfFile3(mapping, process, candidate + view.addressOffset, view.backingOffset, view.size,
                 MEM_REPLACE_PLACEHOLDER, PAGE_READWRITE, nullptr, 0))
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

    // unwind mapped views and both parts of the last split after any failure
    const DWORD error = GetLastError();
    for (size_t i = 0; i < mapped; ++i)
        UnmapViewOfFile(candidate + views[i].addressOffset);
    const auto& pending = views[mapped];
    VirtualFree(candidate + pending.addressOffset, 0, MEM_RELEASE);
    if (split)
        VirtualFree(candidate + pending.addressOffset + pending.size, 0, MEM_RELEASE);
    CloseHandle(mapping);
    SetLastError(error);
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
