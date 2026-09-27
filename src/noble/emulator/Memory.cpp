#include "Memory.h"

#include "Logger.h"
#include "core/platform/WinAlloc.h"
#include <assert.h>

Memory::Memory() : Hostallocator_(std::make_unique<WinAlloc>()) {}

bool Memory::Initialise() {
    uint8_t* memBase = (uint8_t*)Hostallocator_->Reserve((PVOID)kPreferredHostBase, kXboxMemorySize);
    if (memBase == nullptr)
        memBase = (uint8_t*)Hostallocator_->Reserve(nullptr, kXboxMemorySize);

    if (memBase == nullptr) {
        LOG_FATAL("Unable to Allocate sytem memory\n");
        return false;
    }

    mMemoryBase_ = memBase;

    return true;
}

void* Memory::GetMemoryBase() {
    return mMemoryBase_;
}

void* Memory::AllocateVirtual(size_t size) {
    return Hostallocator_->Allocate(size);
}

template <typename T>
inline T Memory::GuestToHostVirtual(uint32_t GuestAddress) {
    uint8_t* HostAddr = mMemoryBase_ + GuestAddress;

    return reinterpret_cast<T>(HostAddr);
}
