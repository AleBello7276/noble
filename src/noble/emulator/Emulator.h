#pragma once

#include "Memory.h"
#include "PPCModule.h"
#include "cpu/Scheduler.h"
#include "kernel/Kernel.h"
#include <string>

class Emulator {
public:
    Emulator();

    /* initliase the Emulator subsytems */
    bool Initialise();

    bool LoadTitle(std::string path);

    void Run();

    void Shutdown();

private:
    Memory mMemory_;
    Scheduler mScheduler_;
    Kernel mKernel_;

    PPCModule mStartModule;
};
