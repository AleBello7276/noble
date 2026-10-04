#pragma once

#include <cstdint>

class GPUBackend {
public:
    struct DisplayMode {
        uint32_t width = 1280;
        uint32_t height = 720;
        bool interlaced = false;
        bool widescreen = true;
        float refreshRate = 60.0f;
    };

    struct InterruptCallback {
        uint32_t routine = 0;
        uint32_t userData = 0;
    };

    struct EngineParameters {
        uint32_t unknown = 0;
        uint32_t cleanupCallback = 0;
        uint32_t callbackArgument = 0;
        uint32_t pfpMicrocode = 0;
        uint32_t meMicrocode = 0;
    };

    virtual ~GPUBackend() = default;

    // initialize the host backend before guest video imports may run
    virtual bool Initialize() = 0;

    // release backend state after guest execution has stopped
    virtual void Shutdown() = 0;

    // return the display mode reported to guest software
    virtual DisplayMode GetDisplayMode() const = 0;

    // configure the guest graphics engines from opaque guest addresses
    virtual bool InitializeEngines(const EngineParameters& parameters) = 0;

    // stop the guest graphics engines while retaining the host backend
    virtual void ShutdownEngines() = 0;

    // replace the guest interrupt callback and user data together
    virtual void SetInterruptCallback(uint32_t routine, uint32_t userData) = 0;

    // copy the registered callback for future interrupt delivery
    virtual InterruptCallback GetInterruptCallback() const = 0;
};
