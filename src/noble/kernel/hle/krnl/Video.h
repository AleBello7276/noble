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

// guest scaler source rectangle with big endian pixel coordinates
struct DisplayRect {
    be<uint32_t> x1;
    be<uint32_t> y1;
    be<uint32_t> x2;
    be<uint32_t> y2;
};
static_assert(sizeof(DisplayRect) == 0x10);

// guest scaler filter coefficients stored as big endian floats
struct DisplayFilterParameters {
    be<float> nyquist;
    be<float> flickerFilter;
    be<float> beta;
};
static_assert(sizeof(DisplayFilterParameters) == 0x0C);

// guest hardware scaler parameters shared by display information queries
struct DisplayScalerParameters {
    DisplayRect sourceRect;
    be<uint32_t> outputWidth;
    be<uint32_t> outputHeight;
    be<uint32_t> verticalFilterType;
    DisplayFilterParameters verticalFilter;
    be<uint32_t> horizontalFilterType;
    DisplayFilterParameters horizontalFilter;
};
static_assert(sizeof(DisplayScalerParameters) == 0x38);
static_assert(offsetof(DisplayScalerParameters, verticalFilter) == 0x1C);
static_assert(offsetof(DisplayScalerParameters, horizontalFilter) == 0x2C);

// guest display information with explicit padding to preserve the xbox abi offsets
struct DisplayInformation {
    be<uint16_t> frontBufferWidth;
    be<uint16_t> frontBufferHeight;
    uint8_t frontBufferColorFormat;
    uint8_t frontBufferPixelFormat;
    uint8_t reserved06[2];
    DisplayScalerParameters scaler;
    be<uint16_t> overscanLeft;
    be<uint16_t> overscanTop;
    be<uint16_t> overscanRight;
    be<uint16_t> overscanBottom;
    be<uint16_t> displayWidth;
    be<uint16_t> displayHeight;
    be<float> refreshRate;
    be<uint32_t> isInterlaced;
    uint8_t displayColorFormat;
    uint8_t reserved55;
    be<uint16_t> actualDisplayWidth;
};
static_assert(sizeof(DisplayInformation) == 0x58);
static_assert(offsetof(DisplayInformation, scaler) == 0x08);
static_assert(offsetof(DisplayInformation, overscanLeft) == 0x40);
static_assert(offsetof(DisplayInformation, displayWidth) == 0x48);
static_assert(offsetof(DisplayInformation, refreshRate) == 0x4C);
static_assert(offsetof(DisplayInformation, isInterlaced) == 0x50);
static_assert(offsetof(DisplayInformation, actualDisplayWidth) == 0x56);

// fill the reported mode shared by the kernel and xam video queries
void QueryVideoMode(Kernel& kernel, VideoMode& videoMode);

// expose the typed video implementations for kernel export registration
std::span<const Export> VideoExports();

}  // namespace hle::krnl
