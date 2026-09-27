#pragma once

#include "core/HostAlloc.h"
#include <memory>

// main memory class
class Memory {
public:
    Memory();

    /* Initialise Memory subsystem */
    bool Initialise();

    /* Get private memory base field */
    void* GetMemoryBase();

    /* Allocate some virtual memory */
    void* AllocateVirtual(size_t size);

    template <typename T = uint8_t*>
    inline T GuestToHostVirtual(uint32_t GuestAddress);

private:
    static constexpr size_t kXboxMemorySize = 0x100000000;  // 4GB
    static constexpr size_t kPreferredHostBase = 0x100000000;

    uint8_t* mMemoryBase_;
    std::unique_ptr<HostAlloc> Hostallocator_;
};
