// derived from xenia gpu definitions, copyright ben vanik and contributors
// see Xenia-LICENSE.txt for redistribution terms

#pragma once

#include "Registers.h"
#include <array>

namespace gpu {

struct RegisterResetValue {
    Register index;
    uint32_t value;
};

// nonzero reset values initialized by xenia register_file
inline constexpr std::array kRegisterResetValues{
    RegisterResetValue{Register::VGT_MAX_VTX_INDX, 0x0000FFFF},
    RegisterResetValue{Register::VGT_MULTI_PRIM_IB_RESET_INDX, 0x0000FFFF},
    RegisterResetValue{Register::PA_SC_SCREEN_SCISSOR_BR, 0x20002000},
    RegisterResetValue{Register::RB_STENCILREFMASK_BF, 0x00FFFF00},
    RegisterResetValue{Register::PA_SU_POINT_SIZE, 0x00080008},
    RegisterResetValue{Register::PA_SU_POINT_MINMAX, 0x04000010},
    RegisterResetValue{Register::PA_SU_LINE_CNTL, 0x00000008},
    RegisterResetValue{Register::PA_SC_LINE_CNTL, 0x00000400},
    RegisterResetValue{Register::VGT_HOS_REUSE_DEPTH, 0x0000000E},
    RegisterResetValue{Register::VGT_VERTEX_REUSE_BLOCK_CNTL, 0x0000000E},
    RegisterResetValue{Register::VGT_OUT_DEALLOC_CNTL, 0x00000010},
    RegisterResetValue{Register::PA_CL_GB_VERT_CLIP_ADJ, 0x40000000},
    RegisterResetValue{Register::PA_CL_GB_VERT_DISC_ADJ, 0x3F800000},
    RegisterResetValue{Register::PA_CL_GB_HORZ_CLIP_ADJ, 0x40000000},
    RegisterResetValue{Register::PA_CL_GB_HORZ_DISC_ADJ, 0x3F800000},
    RegisterResetValue{Register::PA_SC_AA_MASK, 0x0000FFFF},
};

}  // namespace gpu
