#include "NullGPU.h"

bool NullGPU::Initialize() {
    std::lock_guard lock(mutex_);
    if (initialized_)
        return true;

    engineParameters_ = {};
    interruptCallback_ = {};
    enginesInitialized_ = false;
    initialized_ = true;
    return true;
}

void NullGPU::Shutdown() {
    std::lock_guard lock(mutex_);
    engineParameters_ = {};
    interruptCallback_ = {};
    enginesInitialized_ = false;
    initialized_ = false;
}

GPUBackend::DisplayMode NullGPU::GetDisplayMode() const {
    return {};
}

bool NullGPU::InitializeEngines(const EngineParameters& parameters) {
    std::lock_guard lock(mutex_);
    if (!initialized_)
        return false;

    engineParameters_ = parameters;
    enginesInitialized_ = true;
    return true;
}

void NullGPU::ShutdownEngines() {
    std::lock_guard lock(mutex_);
    engineParameters_ = {};
    enginesInitialized_ = false;
}

void NullGPU::SetInterruptCallback(uint32_t routine, uint32_t userData) {
    std::lock_guard lock(mutex_);
    interruptCallback_ = {routine, userData};
}

GPUBackend::InterruptCallback NullGPU::GetInterruptCallback() const {
    std::lock_guard lock(mutex_);
    return interruptCallback_;
}

bool NullGPU::EnginesInitialized() const {
    std::lock_guard lock(mutex_);
    return enginesInitialized_;
}

GPUBackend::EngineParameters NullGPU::GetEngineParameters() const {
    std::lock_guard lock(mutex_);
    return engineParameters_;
}
