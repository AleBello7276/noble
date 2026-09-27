#pragma once

#include "VM/PPCContext.h"

struct HardwareThread {
    PPCContext mCPUContext;
};

/*
    the core Virtual Machine class of the Emulator
*/
class RuntimeVM {
public:
    RuntimeVM();
    ~RuntimeVM();

private:

    // 4GB range of memory the Xbox 360 uses the whole 32-bit address space mapped to physical memory
    void ReserveMemory();

public:

private:
    void* mMainMemory = nullptr;

    // 3 cores * 2 threads 0 should be boot thread
    static constexpr uint32_t NUM_HW_THREADS = 6;
    HardwareThread mHWThreads[NUM_HW_THREADS];
};


