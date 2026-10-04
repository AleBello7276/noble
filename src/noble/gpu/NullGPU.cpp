#include "NullGPU.h"
#include "RegisterDefaults.h"
#include "core/endian.h"
#include "emulator/Memory.h"

NullGPU::~NullGPU() {
    Shutdown();
}

bool NullGPU::Initialize() {
    std::lock_guard lock(mutex_);
    if (initialized_)
        return true;

    if (!memory_.MapDeviceMemory(kRegisterBase, kRegisterSize))
        return false;

    auto* registers = static_cast<be<uint32_t>*>(memory_.Translate(kRegisterBase, kRegisterSize));
    // apply xenia reset values to the cpu visible part of the register file
    for (const auto& reset : gpu::kRegisterResetValues) {
        if (gpu::IsMMIORegister(reset.index))
            registers[static_cast<uint32_t>(reset.index)] = reset.value;
    }
    registers[static_cast<uint32_t>(gpu::Register::RB_EDRAM_TIMING)] = 0x08100748;
    registers[static_cast<uint32_t>(gpu::Register::RB_BC_CONTROL)] = 0x0000200E;
    // report vblank for the null display
    registers[static_cast<uint32_t>(gpu::Register::D1MODE_VBLANK_VLINE_STATUS)] = 1;
    const auto mode = GetDisplayMode();
    registers[static_cast<uint32_t>(gpu::Register::D1MODE_VIEWPORT_SIZE)] = (mode.width << 16) | mode.height;

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
    if (initialized_)
        memory_.FreeVirtual(kRegisterBase);
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
