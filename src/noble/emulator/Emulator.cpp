#include "Emulator.h"
#include "Logger.h"
#include <cstring>

Emulator::Emulator(diagnostics::TraceSink* trace)
    : mMemory_(), cpu_(mMemory_, trace), mScheduler_(cpu_, trace),
      mKernel_(mMemory_, mScheduler_, {}, trace) {
    cpu_.jit()->SetHLERegistry(&mKernel_.Imports());
}

Emulator::~Emulator() {
    Shutdown();
}

bool Emulator::Initialise() {
    if (mMemory_.Initialise() == false)
        return false;

    if (mScheduler_.Initialise() == false)
        return false;

    if (mKernel_.Initialize() == false)
        return false;

    return true;
}

bool Emulator::LoadTitle(std::string path) {
    if (mTitleProcess_)
        return false;

    stopRequested_.store(false, std::memory_order_release);

    // load XEX or EXE
    mStartModule = PPCModule(path, false, false);
    if (!mStartModule.mImage || !mStartModule.mImage->getMemoryData())
        return false;

    auto& image = *mStartModule.mImage;
    const size_t size = image.getMemorySize();
    const uint32_t base = image.getBaseAddress();

    // allocate memory region at image base
    if (!size || !mMemory_.AllocateFixed(base, size))
        return false;

    mImageAddress_ = base;

    // copy executable into memory
    std::memcpy(mMemory_.Translate(base, size), image.getMemoryData(), size);

    // make a kernel process
    mTitleProcess_ = mKernel_.CreateGuestProcess({base, image.getEntryPoint()});

    if (!mTitleProcess_) {
        mMemory_.FreeVirtual(mImageAddress_);
        mImageAddress_ = 0;
        return false;
    }

    if (!mKernel_.SetExecutableModule(image, mStartModule.mPath)) {
        mKernel_.Shutdown();
        mTitleProcess_ = nullptr;
        mMemory_.FreeVirtual(mImageAddress_);
        mImageAddress_ = 0;
        return false;
    }

    // make process main thread
    KThread* initial = mKernel_.CreateInitialThread(mTitleProcess_);
    if (!initial) {
        mKernel_.Shutdown();
        mTitleProcess_ = nullptr;
        mMemory_.FreeVirtual(mImageAddress_);
        mImageAddress_ = 0;
        return false;
    }

    // queue main thread
    mInitialThread_ = initial;
    mKernel_.StartThread(initial);
    return true;
}

bool Emulator::Run() {
    if (!mInitialThread_)
        return false;

    if (stopRequested_.load(std::memory_order_acquire))
        return true;

    // register boot metadata and compile only the guest entry point
    try {
        cpu_.jit()->RegisterPPCModule(mStartModule);
    } catch (const std::exception& error) {
        LOG_ERROR("Unable to bind title imports: {}", error.what());
        return false;
    }

    const GuestAddress entry = mStartModule.mImage->getEntryPoint();
    cpu_.jit()->CompileJITBlock(entry);

    // something went wrong
    if (!cpu_.jit()->FindBlock(entry))
        return false;

    // wake scheduler and wait for main thread to start
    {
        std::lock_guard lock(executionStartMutex_);

        if (stopRequested_.load(std::memory_order_acquire))
            return true;

        mScheduler_.Start();
    }

    mScheduler_.WaitForThread(mInitialThread_);

    return !mInitialThread_->faulted;
}

void Emulator::Shutdown() {
    mScheduler_.Stop();
    mKernel_.Shutdown();

    if (mImageAddress_) {
        mMemory_.FreeVirtual(mImageAddress_);
        mImageAddress_ = 0;
    }

    mTitleProcess_ = nullptr;
    mInitialThread_ = nullptr;
}

void Emulator::RequestStop() {
    stopRequested_.store(true, std::memory_order_release);

    std::lock_guard lock(executionStartMutex_);

    mScheduler_.Stop();
}
