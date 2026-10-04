#include "Video.h"

#include "kernel/Kernel.h"
#include <array>
#include <cstring>
#include <stdexcept>

namespace hle::krnl {

void QueryVideoMode(Kernel& kernel, VideoMode& videoMode) {
    const auto mode = kernel.GPU().GetDisplayMode();
    videoMode = {};
    videoMode.displayWidth = mode.width;
    videoMode.displayHeight = mode.height;
    videoMode.isInterlaced = mode.interlaced;
    videoMode.isWidescreen = mode.widescreen;
    videoMode.isHiDef = uint32_t(videoMode.displayWidth) >= 0x500;
    videoMode.refreshRate = mode.refreshRate;
    videoMode.videoStandard = 1;
    videoMode.pixelRate = 0x8A;
    videoMode.widescreenFlag = uint32_t(videoMode.isWidescreen) ? 0x01 : 0x03;
}

// write the current video mode to the caller supplied guest structure
void VdQueryVideoMode(Kernel& kernel, Pointer<VideoMode> videoMode) {
    QueryVideoMode(kernel, *videoMode);
}

// pass the opaque guest engine parameters to the selected graphics backend
uint32_t VdInitializeEngines(Kernel& kernel, uint32_t unknown, GuestAddress callback,
                             GuestAddress callbackArgument, GuestAddress pfpMicrocode,
                             GuestAddress meMicrocode) {
    return kernel.GPU().InitializeEngines({unknown, callback, callbackArgument, pfpMicrocode, meMicrocode});
}

// stop guest engine state while retaining the host graphics backend
void VdShutdownEngines(Kernel& kernel) {
    kernel.GPU().ShutdownEngines();
}

void VdInitializeRingBuffer(Kernel& kernel, Memory& memory, GuestAddress physicalAddress, int32_t sizeLog2) {
    if (sizeLog2 < 0 || sizeLog2 > 26)
        throw std::invalid_argument("invalid gpu ring buffer size encoding");

    const uint32_t size = uint32_t(1) << (uint32_t(sizeLog2) + 3);
    if (physicalAddress > 0x20000000u - size)
        throw std::out_of_range("gpu ring buffer exceeds physical memory");

    // use a physical alias so physical address zero is valid and permissions are checked
    const GuestAddress address = 0xA0000000u + physicalAddress;
    if (!memory.IsAccessible(address, size, true))
        throw std::out_of_range("gpu ring buffer is not in writable physical memory");

    std::memset(memory.Translate(address, size), 0, size);
    kernel.GPU().InitializeRingBuffer({physicalAddress, uint32_t(sizeLog2)});
}

// configure future physical read pointer writes without changing guest memory during registration
void VdEnableRingBufferRPtrWriteBack(Kernel& kernel, Memory& memory, GuestAddress physicalAddress,
                                     int32_t blockSizeLog2) {
    if (blockSizeLog2 < 0 || blockSizeLog2 > 19)
        throw std::invalid_argument("invalid gpu read pointer writeback block encoding");

    if (physicalAddress) {
        if (physicalAddress > 0x1FFFFFFCu
            || !memory.IsAccessible(0xA0000000u + physicalAddress, sizeof(uint32_t), true))
            throw std::out_of_range("gpu read pointer writeback is not in writable physical memory");
    }

    kernel.GPU().EnableReadPointerWriteBack({physicalAddress, uint32_t(blockSizeLog2)});
}

// retain guest callback addresses without invoking or dereferencing them during registration
void VdSetGraphicsInterruptCallback(Kernel& kernel, GuestAddress callback, GuestAddress userData) {
    kernel.SetGraphicsInterruptCallback(callback, userData);
}

// register the opaque guest identifier address without modifying guest memory
void VdSetSystemCommandBufferGpuIdentifierAddress(Kernel& kernel, GuestAddress address) {
    kernel.GPU().SetSystemCommandBufferGpuIdentifierAddress(address);
}

constexpr std::array exports{
    Bind<&VdQueryVideoMode>(XboxLibrary::XboxKrnl, "VdQueryVideoMode"),
    Bind<&VdInitializeEngines>(XboxLibrary::XboxKrnl, "VdInitializeEngines"),
    Bind<&VdInitializeRingBuffer>(XboxLibrary::XboxKrnl, "VdInitializeRingBuffer"),
    Bind<&VdEnableRingBufferRPtrWriteBack>(XboxLibrary::XboxKrnl, "VdEnableRingBufferRPtrWriteBack"),
    Bind<&VdShutdownEngines>(XboxLibrary::XboxKrnl, "VdShutdownEngines"),
    Bind<&VdSetGraphicsInterruptCallback>(XboxLibrary::XboxKrnl, "VdSetGraphicsInterruptCallback"),
    Bind<&VdSetSystemCommandBufferGpuIdentifierAddress>(XboxLibrary::XboxKrnl,
                                                        "VdSetSystemCommandBufferGpuIdentifierAddress"),
};

std::span<const Export> VideoExports() {
    return exports;
}

}  // namespace hle::krnl
