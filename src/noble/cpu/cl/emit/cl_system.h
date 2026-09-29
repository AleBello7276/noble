#pragma once

#include "cl_util.h"

CLHandler(mfspr) {
    uint32_t spr = info_.mInst.field_spr();

    switch (spr) {
    case eSPR::LR:
        emitter_.StoreGPR(info_.mInst.field_rd(), emitter_.LoadSPR(eSPR::LR));
        return;
    default:
        LOG_ERROR("mfspr : Unknown SPR register {}", spr);
        return;
    }
}
