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

    void SetInterruptCallback(uint32_t routine, uint32_t userData) override;

    InterruptCallback GetInterruptCallback() const override;

    bool EnginesInitialized() const;

    EngineParameters GetEngineParameters() const;

private:
    mutable std::mutex mutex_;
    bool initialized_ = false;
    bool enginesInitialized_ = false;
    EngineParameters engineParameters_;
    InterruptCallback interruptCallback_;
};
