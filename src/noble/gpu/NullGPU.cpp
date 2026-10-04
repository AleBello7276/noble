#include "NullGPU.h"

bool NullGPU::Initialize() {
    std::lock_guard lock(mutex_);
    if (initialized_)
        return true;

    engineParameters_ = {};
    ringBuffer_ = {};
    readPointerWriteBack_ = {};
    ringBufferInitialized_ = false;
    interruptCallback_ = {};
    systemCommandBufferGpuIdentifierAddress_ = 0;
    enginesInitialized_ = false;
    initialized_ = true;
    return true;
}

void NullGPU::Shutdown() {
    std::lock_guard lock(mutex_);
    engineParameters_ = {};
    ringBuffer_ = {};
    readPointerWriteBack_ = {};
    ringBufferInitialized_ = false;
    interruptCallback_ = {};
    systemCommandBufferGpuIdentifierAddress_ = 0;
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
    ringBuffer_ = {};
    readPointerWriteBack_ = {};
    ringBufferInitialized_ = false;
    enginesInitialized_ = false;
}

void NullGPU::InitializeRingBuffer(const RingBuffer& parameters) {
    std::lock_guard lock(mutex_);
    ringBuffer_ = parameters;
    ringBufferInitialized_ = true;
}

GPUBackend::RingBuffer NullGPU::GetRingBuffer() const {
    std::lock_guard lock(mutex_);
    return ringBuffer_;
}

bool NullGPU::RingBufferInitialized() const {
    std::lock_guard lock(mutex_);
    return ringBufferInitialized_;
}

void NullGPU::EnableReadPointerWriteBack(const ReadPointerWriteBack& parameters) {
    std::lock_guard lock(mutex_);
    readPointerWriteBack_ = parameters;
}

GPUBackend::ReadPointerWriteBack NullGPU::GetReadPointerWriteBack() const {
    std::lock_guard lock(mutex_);
    return readPointerWriteBack_;
}

void NullGPU::SetInterruptCallback(uint32_t routine, uint32_t userData) {
    std::lock_guard lock(mutex_);
    interruptCallback_ = {routine, userData};
}

GPUBackend::InterruptCallback NullGPU::GetInterruptCallback() const {
    std::lock_guard lock(mutex_);
    return interruptCallback_;
}

void NullGPU::SetSystemCommandBufferGpuIdentifierAddress(uint32_t address) {
    std::lock_guard lock(mutex_);
    systemCommandBufferGpuIdentifierAddress_ = address;
}

uint32_t NullGPU::GetSystemCommandBufferGpuIdentifierAddress() const {
    std::lock_guard lock(mutex_);
    return systemCommandBufferGpuIdentifierAddress_;
}

bool NullGPU::EnginesInitialized() const {
    std::lock_guard lock(mutex_);
    return enginesInitialized_;
}

GPUBackend::EngineParameters NullGPU::GetEngineParameters() const {
    std::lock_guard lock(mutex_);
    return engineParameters_;
}
