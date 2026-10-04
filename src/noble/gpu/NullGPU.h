#pragma once

#include "GPUBackend.h"
#include <mutex>

// retain graphics state without rendering or executing gpu command buffers
class NullGPU final : public GPUBackend {
public:
    bool Initialize() override;

    void Shutdown() override;

    DisplayMode GetDisplayMode() const override;

    bool InitializeEngines(const EngineParameters& parameters) override;

    void ShutdownEngines() override;

    void InitializeRingBuffer(const RingBuffer& parameters) override;

    RingBuffer GetRingBuffer() const;

    bool RingBufferInitialized() const;

    void EnableReadPointerWriteBack(const ReadPointerWriteBack& parameters) override;

    ReadPointerWriteBack GetReadPointerWriteBack() const;

    void SetInterruptCallback(uint32_t routine, uint32_t userData) override;

    InterruptCallback GetInterruptCallback() const override;

    void SetSystemCommandBufferGpuIdentifierAddress(uint32_t address) override;

    uint32_t GetSystemCommandBufferGpuIdentifierAddress() const override;

    bool EnginesInitialized() const;

    EngineParameters GetEngineParameters() const;

private:
    mutable std::mutex mutex_;
    bool initialized_ = false;
    bool enginesInitialized_ = false;
    EngineParameters engineParameters_;
    RingBuffer ringBuffer_;
    ReadPointerWriteBack readPointerWriteBack_;
    bool ringBufferInitialized_ = false;
    InterruptCallback interruptCallback_;
    uint32_t systemCommandBufferGpuIdentifierAddress_ = 0;
};
