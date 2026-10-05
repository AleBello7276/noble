#pragma once

#include <array>
#include <stdint.h>

/*
    some data structures and type definitions are taken from:
    https://github.com/xenon-emu/xenon/blob/main/Xenon/Core/XCPU/PPU/PowerPC.h
*/

// Link Register
typedef uint64_t LR_t;

typedef uint64_t MSR_t;

// Count Register
typedef uint64_t CTR_t;

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
    uint64_t u64 = 0;
    int64_t s64;
};

/* Floating Point Register */
union FPR {
    float f32;
    double f64;
};

struct cr_field {
    uint8_t BIT0;
    uint8_t BIT1;
    uint8_t BIT2;
    uint8_t BIT3;
};

/* Control Register */
union CR {
    uint8_t bits[32];
    uint32_t fields[8];
    struct {
        uint8_t LT;
        uint8_t GT;
        uint8_t EQ;
        uint8_t SO;
    } CR0;
    struct {
        uint8_t FX;
        uint8_t FEX;
        uint8_t VX;
        uint8_t OX;
    } CR1;
    cr_field CR2;
    cr_field CR3;
    cr_field CR4;
    cr_field CR5;
    cr_field CR6;
    cr_field CR7;
};

// union CR {
//     uint32_t CRFull;
//     uint8_t bits[32];
//     struct {
//         uint32_t CR7 : 4;
//         uint32_t CR6 : 4;
//         uint32_t CR5 : 4;
//         uint32_t CR4 : 4;
//         uint32_t CR3 : 4;
//         uint32_t CR2 : 4;
//         uint32_t CR1 : 4;
//         uint32_t CR0 : 4;
//     };
// };

enum eSPR : uint16_t {
    XER = 1,
    LR = 8,
    CTR = 9,
};

// union uXER {
//     uint32_t hexValue;
// #if  defined(__LITTLE_ENDIAN__) || defined(_WIN32) \
//    || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
//     struct {
//         uint32_t ByteCount : 7;
//         uint32_t R0 : 22;
//         uint32_t CA : 1;
//         uint32_t OV : 1;
//         uint32_t SO : 1;
//     };
// #else
//     struct {
//         uint32_t SO : 1;
//         uint32_t OV : 1;
//         uint32_t CA : 1;
//         uint32_t R0 : 22;
//         uint32_t ByteCount : 7;
//     };
// #endif
// };

struct XERr {
    bool CA;
    bool OV;
    bool SO;
};

struct SPRState {
    LR_t LR;
    CTR_t CTR;
    XERr XER;
};

// https://github.com/xenia-canary/xenia-canary/blob/canary_experimental/src/xenia/cpu/ppc/ppc_context.h#L374
struct FPSCRr {
    union {
        uint32_t value;
        struct {
            uint32_t rn : 2;      // FP rounding control: 00 = nearest
                                  //                      01 = toward zero
                                  //                      10 = toward +infinity
                                  //                      11 = toward -infinity
            uint32_t ni : 1;      // Floating-point non-IEEE mode
            uint32_t xe : 1;      // IEEE floating-point inexact exception enable
            uint32_t ze : 1;      // IEEE floating-point zero divide exception enable
            uint32_t ue : 1;      // IEEE floating-point underflow exception enable
            uint32_t oe : 1;      // IEEE floating-point overflow exception enable
            uint32_t ve : 1;      // FP invalid op exception enable
            uint32_t vxcvi : 1;   // FP invalid op exception: invalid integer convert
                                  // -- sticky
            uint32_t vxsqrt : 1;  // FP invalid op exception: invalid sqrt -- sticky
            uint32_t vxsoft : 1;  // FP invalid op exception: software request
                                  // -- sticky
            uint32_t reserved : 1;
            uint32_t fprf_un : 1;  // FP result unordered or NaN (FU or ?)
            uint32_t fprf_eq : 1;  // FP result equal or zero (FE or =)
            uint32_t fprf_gt : 1;  // FP result greater than or positive (FG or >)
            uint32_t fprf_lt : 1;  // FP result less than or negative (FL or <)
            uint32_t fprf_c : 1;   // FP result class
            uint32_t fi : 1;       // FP fraction inexact
            uint32_t fr : 1;       // FP fraction rounded
            uint32_t vxvc : 1;     // FP invalid op exception: invalid compare         --
                                   // sticky
            uint32_t vximz : 1;    // FP invalid op exception: infinity * 0 -- sticky
            uint32_t vxzdz : 1;    // FP invalid op exception: 0 / 0 -- sticky
            uint32_t vxidi : 1;    // FP invalid op exception: infinity / infinity
                                   // -- sticky
            uint32_t vxisi : 1;    // FP invalid op exception: infinity - infinity
                                   // -- sticky
            uint32_t vxsnan : 1;   // FP invalid op exception: SNaN -- sticky
            uint32_t xx : 1;       // FP inexact exception                             -- sticky
            uint32_t zx : 1;       // FP zero divide exception                         -- sticky
            uint32_t ux : 1;       // FP underflow exception                           -- sticky
            uint32_t ox : 1;       // FP overflow exception                            -- sticky
            uint32_t vx : 1;       // FP invalid operation exception summary
            uint32_t fex : 1;      // FP enabled exception summary
            uint32_t fx : 1;       // FP exception summary                             -- sticky
        } bits;
    } fpscr;  // Floating-point status and control register
};

class KThread;

enum class PPCFault : uint32_t { None, UnimplementedImport, HLEFailure, UncompiledTarget, MemoryAccess };

enum class HostAction : uint32_t { None, Wait, Yield };

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
    uint32_t NIA = 0;
    SPRState SPRs;
    FPSCRr FPSCR;
    MSR_t MSR;

    uint32_t reserve_address;
    uint32_t reserve_value;
    uint8_t reserve_valid;

    // retain host execution metadata outside the guest architectural register file
    KThread* HostThread = nullptr;
    PPCFault Fault = PPCFault::None;
    uint32_t FaultAddress = 0;
    // request a dispatcher exit after returning from a host service
    HostAction Action = HostAction::None;

    /* continue... */
};
