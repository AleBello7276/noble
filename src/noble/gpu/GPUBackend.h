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

    struct AspectRatio {
        uint64_t width = 16;
        uint64_t height = 9;
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

    struct RingBuffer {
        uint32_t physicalAddress = 0;
        // hardware size encoding with byte size equal to 1 << (sizeLog2 + 3)
        uint32_t sizeLog2 = 0;
    };

    struct ReadPointerWriteBack {
        // physical address zero disables writeback as in xenia
        uint32_t physicalAddress = 0;
        uint32_t blockSizeLog2 = 0;
    };

    virtual ~GPUBackend() = default;

    // initialize the host backend before guest video imports may run
    virtual bool Initialize() = 0;

    // release backend state after guest execution has stopped
    virtual void Shutdown() = 0;

    // return the display mode reported to guest software
    virtual DisplayMode GetDisplayMode() const = 0;

    // retain the presentation aspect after guest hardware scaling
    virtual void SetScaledAspectRatio(AspectRatio aspect) = 0;

    // copy the current presentation aspect for display backends
    virtual AspectRatio GetScaledAspectRatio() const = 0;

    // configure the guest graphics engines from opaque guest addresses
    virtual bool InitializeEngines(const EngineParameters& parameters) = 0;

    // stop the guest graphics engines while retaining the host backend
    virtual void ShutdownEngines() = 0;

    // replace the primary ring buffer parameters and reset command processor read state
    virtual void InitializeRingBuffer(const RingBuffer& parameters) = 0;

    // register the physical read pointer destination and its hardware update block encoding
    virtual void EnableReadPointerWriteBack(const ReadPointerWriteBack& parameters) = 0;

    // replace the guest interrupt callback and user data together
    virtual void SetInterruptCallback(uint32_t routine, uint32_t userData) = 0;

    // copy the registered callback for future interrupt delivery
    virtual InterruptCallback GetInterruptCallback() const = 0;

    // retain the guest address supplied for the system command buffer gpu identifier
    virtual void SetSystemCommandBufferGpuIdentifierAddress(uint32_t address) = 0;

    // return the registered identifier address for future command buffer handling
    virtual uint32_t GetSystemCommandBufferGpuIdentifierAddress() const = 0;
};
