// derived from xenia gpu definitions, copyright ben vanik and contributors
// see Xenia-LICENSE.txt for redistribution terms

#pragma once

#include <cstdint>

namespace gpu::xenos {

enum class BlendFactor : uint32_t {
    kZero = 0,
    kOne = 1,
    kSrcColor = 4,
    kOneMinusSrcColor = 5,
    kSrcAlpha = 6,
    kOneMinusSrcAlpha = 7,
    kDstColor = 8,
    kOneMinusDstColor = 9,
    kDstAlpha = 10,
    kOneMinusDstAlpha = 11,
    kConstantColor = 12,
    kOneMinusConstantColor = 13,
    kConstantAlpha = 14,
    kOneMinusConstantAlpha = 15,
    kSrcAlphaSaturate = 16,

};

enum class BlendOp : uint32_t {
    kAdd = 0,
    kSubtract = 1,
    kMin = 2,
    kMax = 3,
    kRevSubtract = 4,
};

enum class ColorFormat : uint32_t {
    k_8 = 2,
    k_1_5_5_5 = 3,
    k_5_6_5 = 4,
    k_6_5_5 = 5,
    k_8_8_8_8 = 6,
    k_2_10_10_10 = 7,
    k_8_A = 8,
    k_8_B = 9,
    k_8_8 = 10,
    k_8_8_8_8_A = 14,
    k_4_4_4_4 = 15,
    k_10_11_11 = 16,
    k_11_11_10 = 17,
    k_16 = 24,
    k_16_16 = 25,
    k_16_16_16_16 = 26,
    k_16_FLOAT = 30,
    k_16_16_FLOAT = 31,
    k_16_16_16_16_FLOAT = 32,
    k_32_FLOAT = 36,
    k_32_32_FLOAT = 37,
    k_32_32_32_32_FLOAT = 38,
    k_8_8_8_8_AS_16_16_16_16 = 50,
    k_2_10_10_10_AS_16_16_16_16 = 54,
    k_10_11_11_AS_16_16_16_16 = 55,
    k_11_11_10_AS_16_16_16_16 = 56,
};

enum class ColorRenderTargetFormat : uint32_t {
    k_8_8_8_8 = 0,
    k_8_8_8_8_GAMMA = 1,
    k_2_10_10_10 = 2,

    k_2_10_10_10_FLOAT = 3,

    k_16_16 = 4,

    k_16_16_16_16 = 5,
    k_16_16_FLOAT = 6,
    k_16_16_16_16_FLOAT = 7,
    k_2_10_10_10_AS_10_10_10_10 = 10,

    k_2_10_10_10_FLOAT_AS_16_16_16_16 = 12,
    k_32_FLOAT = 14,
    k_32_32_FLOAT = 15,
};

enum class CompareFunction : uint32_t {
    kNever = 0b000,
    kLess = 0b001,
    kEqual = 0b010,
    kLessEqual = 0b011,
    kGreater = 0b100,
    kNotEqual = 0b101,
    kGreaterEqual = 0b110,
    kAlways = 0b111,
};

enum class CopyCommand : uint32_t {
    kRaw = 0,
    kConvert = 1,
    kConstantOne = 2,
    kNull = 3,
};

enum class CopySampleSelect : uint32_t {
    k0,
    k1,
    k2,
    k3,
    k01,
    k23,
    k0123,
};

enum class DepthRenderTargetFormat : uint32_t {
    kD24S8 = 0,

    kD24FS8 = 1,
};

enum class EdramMode : uint32_t {
    kNoOperation = 0,
    kColorDepth = 4,

    kDepthOnly = 5,
    kCopy = 6,
};

enum class Endian : uint32_t {
    kNone = 0,
    k8in16 = 1,
    k8in32 = 2,
    k16in32 = 3,
};

enum class Endian128 : uint32_t {
    kNone = 0,
    k8in16 = 1,
    k8in32 = 2,
    k16in32 = 3,
    k8in64 = 4,
    k8in128 = 5,
};

enum class IndexFormat : uint32_t {
    kInt16,

    kInt32,
};

enum class MajorMode : uint32_t {
    kImplicit,
    kExplicit,
};

enum class MsaaSamples : uint32_t {
    k1X = 0,
    k2X = 1,
    k4X = 2,
};

enum class PixelCenter : uint32_t {

    kD3DZero = 0,

    kOGLHalf = 1,
};

enum class PolygonModeEnable : uint32_t {
    kDisabled = 0,
    kDualMode = 1,

};

enum class PolygonType : uint32_t {
    kPoints = 0,
    kLines = 1,
    kTriangles = 2,
};

enum class PrimitiveType : uint32_t {
    kNone = 0x00,
    kPointList = 0x01,
    kLineList = 0x02,
    kLineStrip = 0x03,
    kTriangleList = 0x04,
    kTriangleFan = 0x05,
    kTriangleStrip = 0x06,
    kTriangleWithWFlags = 0x07,
    kRectangleList = 0x08,
    kUnused1 = 0x09,
    kUnused2 = 0x0A,
    kUnused3 = 0x0B,
    kLineLoop = 0x0C,
    kQuadList = 0x0D,
    kQuadStrip = 0x0E,
    kPolygon = 0x0F,

    kExplicitMajorModeForceStart = 0x10,

    k2DCopyRectListV0 = 0x10,
    k2DCopyRectListV1 = 0x11,
    k2DCopyRectListV2 = 0x12,
    k2DCopyRectListV3 = 0x13,
    k2DFillRectList = 0x14,
    k2DLineStrip = 0x15,
    k2DTriStrip = 0x16,

    kLinePatch = 0x10,
    kTrianglePatch = 0x11,
    kQuadPatch = 0x12,
};

enum class SampleControl : uint32_t {
    kCentroidsOnly = 0,
    kCentersOnly = 1,
    kCentroidsAndCenters = 2,
};

enum class SourceSelect : uint32_t {
    kDMA,
    kImmediate,
    kAutoIndex,
};

enum class StencilOp : uint32_t {
    kKeep = 0,
    kZero = 1,
    kReplace = 2,
    kIncrementClamp = 3,
    kDecrementClamp = 4,
    kInvert = 5,
    kIncrementWrap = 6,
    kDecrementWrap = 7,
};

enum class SurfaceNumberFormat : uint32_t {
    kUnsignedRepeatingFraction = 0,

    kSignedRepeatingFraction = 1,
    kUnsignedInteger = 2,
    kSignedInteger = 3,
    kFloat = 7,
};

enum class TessellationMode : uint32_t {
    kDiscrete = 0,
    kContinuous = 1,
    kAdaptive = 2,
};

enum class VGTOutputPath : uint32_t {
    kVertexReuse = 0,
    kTessellationEnable = 1,
    kPassthru = 2,
};

enum class VertexQuantization : uint32_t {
    k_1_16th = 0,
    k_1_8th = 1,
    k_1_4th = 2,
    k_1_2 = 3,
    k_1 = 4,

};

enum class VertexRounding : uint32_t {
    kTruncate = 0,
    kRound = 1,
    kRoundToEven = 2,
    kRoundToOdd = 3,
};

enum class VertexShaderExportMode : uint32_t {
    kPosition1Vector = 0,
    kPosition2VectorsSprite = 2,
    kPosition2VectorsEdge = 3,
    kPosition2VectorsKill = 4,
    kPosition2VectorsSpriteKill = 5,
    kPosition2VectorsEdgeKill = 6,

    kMultipass = 7,
};

}  // namespace gpu::xenos
