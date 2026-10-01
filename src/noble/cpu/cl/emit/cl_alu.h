#pragma once

#include "cl_util.h"

CLHandler(addis) {
    const auto rt = info_.mInst.field_rd();
    const auto ra = info_.mInst.field_ra();
    const auto uimm = info_.mInst.field_uimm();

    const auto shifted = sign_extend<16>(uimm) << 16;
    const Value immediate = e_.i64(shifted);
    Value toStore;

    // if 0 it's LI so it skips this
    if (ra != 0)
        toStore = e_.ins().iadd(e_.load_gpr(ra), immediate);

    e_.store_gpr(rt, toStore);
}

CLHandler(or_) {
    const auto rt = info_.mInst.field_rd();
    const auto ra = info_.mInst.field_ra();
    const auto uimm = info_.mInst.field_uimm();

    const auto shifted = sign_extend<16>(uimm) << 16;
    const Value immediate = e_.i64(shifted);
    Value toStore;

    // if 0 it's LI so it skips this
    if (ra != 0)
        toStore = e_.ins().iadd(e_.load_gpr(ra), immediate);

    e_.store_gpr(rt, toStore);
}
