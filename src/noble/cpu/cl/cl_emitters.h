#pragma once

#include "cl_util.h"

CLHandler(illegal) {
    LOG_ERROR("illegal instruction, Data: 0x{:08X} Address: 0x{:08X}", info.mInst.code, info.mAddress);
    LOG_TRACE("\t => {}\n", info.mInst.basic().to_string());
    assert(false);
    return;
}
