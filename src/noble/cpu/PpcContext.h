#pragma once

#include <array>
#include <stdint.h>

// https://github.com/xenon-emu/xenon/blob/main/Xenon/Base/Vector128.h#L11
struct alignas(16) Vector128 {
    union {
        struct {
            float x;
            float y;
            float z;
            float w;
        };
        struct {
            int32_t ix;
            int32_t iy;
            int32_t iz;
            int32_t iw;
        };
        struct {
            uint32_t ux;
            uint32_t uy;
            uint32_t uz;
            uint32_t uw;
        };

        std::array<uint64_t, 2> qword;
        std::array<int64_t, 2> qsword;
        std::array<double, 2> dbl;
        std::array<float, 4> flt;
        std::array<uint32_t, 4> dword;
        std::array<int32_t, 4> dsword;
        std::array<uint16_t, 8> word;
        std::array<int16_t, 8> sword;
        std::array<uint8_t, 16> bytes;
    };
};

/* General Purpose Register */
union GPR {
    uint8_t u8;
    int8_t s8;
    uint16_t u16;
    int16_t s16;
    uint32_t u32;
    int32_t s32;
    uint64_t u64;
    int64_t s64;
};

/* Floating Point Register */
union FPR {
    float f32;
    double f64;
};

/* Control Register */
union CR {
    uint32_t CRFull;
    uint8_t bits[32];
    struct {
        uint32_t CR7 : 4;
        uint32_t CR6 : 4;
        uint32_t CR5 : 4;
        uint32_t CR4 : 4;
        uint32_t CR3 : 4;
        uint32_t CR2 : 4;
        uint32_t CR1 : 4;
        uint32_t CR0 : 4;
    };
};

// a Xenon Register File
struct PPCContext {
    static constexpr size_t GPR_COUNT = 32;
    static constexpr size_t FPR_COUNT = 32;
    static constexpr size_t VR_COUNT = 128;

    GPR GPRs[GPR_COUNT]{};
    FPR FPRs[FPR_COUNT]{};
    Vector128 VRs[VR_COUNT]{};
    CR ControlRegister{};
    uint32_t CIA = 0;
    uint32_t LR = 0;

    /* continue... */
};
