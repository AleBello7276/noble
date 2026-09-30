#pragma once

#include "Memory.h"

#include "cpu/Scheduler.h"
#include "kernel/Kernel.h"
#include <string>

class Emulator {
public:
    Emulator();
    ~Emulator();

    /* initliase the Emulator subsytems */
    bool Initialise();

    bool LoadTitle(std::string path);

    bool Run();

    void Shutdown();

private:
    Memory mMemory_;
    CpuExecutor cpu_;
    Scheduler mScheduler_;
    Kernel mKernel_;

    PPCModule mStartModule;
    KProcess* mTitleProcess_ = nullptr;
    KThread* mInitialThread_ = nullptr;
    uint32_t mImageAddress_ = 0;
};
