#include "Video.h"

#include "kernel/Kernel.h"
#include <array>

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

// retain guest callback addresses without invoking or dereferencing them during registration
void VdSetGraphicsInterruptCallback(Kernel& kernel, GuestAddress callback, GuestAddress userData) {
    kernel.SetGraphicsInterruptCallback(callback, userData);
}

constexpr std::array exports{
    Bind<&VdQueryVideoMode>(XboxLibrary::XboxKrnl, "VdQueryVideoMode"),
    Bind<&VdInitializeEngines>(XboxLibrary::XboxKrnl, "VdInitializeEngines"),
    Bind<&VdShutdownEngines>(XboxLibrary::XboxKrnl, "VdShutdownEngines"),
    Bind<&VdSetGraphicsInterruptCallback>(XboxLibrary::XboxKrnl, "VdSetGraphicsInterruptCallback"),
};

std::span<const Export> VideoExports() {
    return exports;
}

}  // namespace hle::krnl
