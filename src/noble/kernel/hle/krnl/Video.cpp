#include "Video.h"

#include <array>

namespace hle::krnl {

void QueryVideoMode(VideoMode& videoMode) {
    // return with fixed mode until display configuration is implemented
    videoMode = {};
    videoMode.displayWidth = 1280;
    videoMode.displayHeight = 720;
    videoMode.isInterlaced = 0;
    videoMode.isWidescreen = 1;
    videoMode.isHiDef = uint32_t(videoMode.displayWidth) >= 0x500;
    videoMode.refreshRate = 60.0f;
    videoMode.videoStandard = 1;
    videoMode.pixelRate = 0x8A;
    videoMode.widescreenFlag = uint32_t(videoMode.isWidescreen) ? 0x01 : 0x03;
}

// write the current video mode to the caller supplied guest structure
void VdQueryVideoMode(Pointer<VideoMode> videoMode) {
    QueryVideoMode(*videoMode);
}

constexpr std::array exports{
    Bind<&VdQueryVideoMode>(XboxLibrary::XboxKrnl, "VdQueryVideoMode"),
};

std::span<const Export> VideoExports() {
    return exports;
}

}  // namespace hle::krnl
