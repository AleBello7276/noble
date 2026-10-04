#pragma once

#include "core/endian.h"
#include "kernel/hle/Exports.h"
#include <cstddef>

namespace hle::krnl {

// guest video mode with big endian scalar fields at the xbox abi offsets
struct VideoMode {
    be<uint32_t> displayWidth;
    be<uint32_t> displayHeight;
    be<uint32_t> isInterlaced;
    be<uint32_t> isWidescreen;
    be<uint32_t> isHiDef;
    be<float> refreshRate;
    be<uint32_t> videoStandard;
    be<uint32_t> pixelRate;
    be<uint32_t> widescreenFlag;
    be<uint32_t> reserved[3];
};
static_assert(sizeof(VideoMode) == 0x30);
static_assert(offsetof(VideoMode, refreshRate) == 0x14);
static_assert(offsetof(VideoMode, videoStandard) == 0x18);
static_assert(offsetof(VideoMode, reserved) == 0x24);

// fill the reported mode shared by the kernel and xam video queries
void QueryVideoMode(Kernel& kernel, VideoMode& videoMode);

// expose the typed video implementations for kernel export registration
std::span<const Export> VideoExports();

}  // namespace hle::krnl
