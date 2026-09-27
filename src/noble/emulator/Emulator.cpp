#include "Emulator.h"

Emulator::Emulator() : mMemory_(), mScheduler_(), mKernel_(&mMemory_, &mScheduler_) {}

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
    mStartModule = PPCModule(path, false, false);

    return true;
}

void Emulator::Run() {
    mScheduler_.Start();
}

void Emulator::Shutdown() {}
