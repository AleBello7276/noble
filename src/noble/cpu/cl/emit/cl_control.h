#pragma once

#include "cl_util.h"

CLHandler(b) {
    const auto lk = info_.mInst.field_lk();
    const auto aa = info_.mInst.field_lk();
    const auto li = info_.mInst.field_li();

    assert(aa);  // NOT YET IMPLEMENTED

    GuestAddress NIA = info_.mAddress + sign_extend<26>(li);

    if (lk) {
        Value return_address = e_.i64(info_.mAddress + 4);
        e_.store_spr(eSPR::LR, return_address);
    }

    bool recursive = (NIA == e_.mFuncRanges.mStart) && lk ? true : false;

    Block label = e_.BlockLookup(NIA);
    if (label != INVALID_ID) {
        e_.Jump(label);
    } else {
        auto func = e_.backend->LookupFunction(NIA);
        e_.CallGuest(e_.builder.declare_func_in_func(e_.jit, func.m_id));
    }
}
