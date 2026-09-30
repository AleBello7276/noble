#pragma once

#include "cl_util.h"

CLHandler(mfspr) {
    const auto spr = static_cast<eSPR>(info_.mInst.field_spr());

    e_.store_gpr(info_.mInst.field_rd(), e_.load_spr(spr));
}

CLHandler(mtspr) {
    const auto spr = static_cast<eSPR>(info_.mInst.field_spr());

    auto value = e_.load_gpr(info_.mInst.field_rs());
    e_.store_spr(spr, value);
}
