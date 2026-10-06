#pragma once

#include "Memory.h"
#include "config/Settings.h"

#include "cpu/Scheduler.h"
#include "diagnostics/Trace.h"
#include "kernel/Kernel.h"
#include <string>

class Emulator {
public:
    explicit Emulator(diagnostics::TraceSink* trace = nullptr, debugger::Debugger* debugger = nullptr,
                      const config::Settings& settings = {});
    ~Emulator();

    /* initliase the Emulator subsytems */
    bool Initialise();

    bool LoadTitle(std::string path);

    bool Run();

    // stop scheduling guest work without freeing storage used by the execution caller
    void RequestStop();

    void Shutdown();

private:
    std::atomic_bool stopRequested_ = false;
    std::mutex executionStartMutex_;
    Memory mMemory_;
    CpuExecutor cpu_;
    Scheduler mScheduler_;
    Kernel mKernel_;

    PPCModule mStartModule;
    KProcess* mTitleProcess_ = nullptr;
    KThread* mInitialThread_ = nullptr;
    uint32_t mImageAddress_ = 0;
};
