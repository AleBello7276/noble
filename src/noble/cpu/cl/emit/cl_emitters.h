#pragma once

#include "cl_util.h"

#include "cl_alu.h"
#include "cl_system.h"

CLHandler(illegal) {
    LOG_ERROR("illegal instruction ==>");
    LOG_TRACE("0x{:08X} {:08X} : {}\n", info_.mAddress, info_.mInst.code, info_.mInst.basic().to_string());
    // assert(false);
    return;
}
