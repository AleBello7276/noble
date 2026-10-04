#pragma once

#include "cl_util.h"

CLHandler(fmul) {
    const auto frt = info_.mInst.field_frd();
    const auto fra = info_.mInst.field_fra();
    const auto frc = info_.mInst.field_frc();
    const auto rc = info_.mInst.field_rc();

    const Value res = e_.ins().fmul(e_.load_fpr(fra), e_.load_fpr(frc));

    e_.store_fpr(frt, res);
    e_.update_fpscr(rc);
}

CLHandler(fmuls) {
    const auto frt = info_.mInst.field_frd();
    const auto fra = info_.mInst.field_fra();
    const auto frc = info_.mInst.field_frc();
    const auto rc = info_.mInst.field_rc();

    Value res = e_.ins().fmul(e_.load_fpr(fra), e_.load_fpr(frc));

    // round to single but store as double
    res = e_.ins().fpromote(types::F64(), e_.ins().fdemote(types::F32(), res));

    e_.store_fpr(frt, res);
    e_.update_fpscr(rc);
}

// TODO: fcvt_from_sint always rounds nearest/ties-even
// ISA fcfid must honor FPSCR.RN and update FPRF/FR/FI/XX
CLHandler(fcfid) {
    const auto frt = info_.mInst.field_frd();
    const auto frb = info_.mInst.field_frb();
    const auto rc = info_.mInst.field_rc();

    const Value bits = e_.ins().bitcast(types::I64(), MemFlags{}, e_.load_fpr(frb));
    const Value res = e_.ins().fcvt_from_sint(types::F64(), bits);

    e_.store_fpr(frt, res);
    e_.update_fpscr(rc);
}

CLHandler(fdiv) {
    const auto frt = info_.mInst.field_frd();
    const auto fra = info_.mInst.field_fra();
    const auto frb = info_.mInst.field_frb();
    const auto rc = info_.mInst.field_rc();

    const Value res = e_.ins().fdiv(e_.load_fpr(fra), e_.load_fpr(frb));

    e_.store_fpr(frt, res);
    e_.update_fpscr(rc);
}

CLHandler(fmr) {
    const auto frt = info_.mInst.field_frd();
    const auto frb = info_.mInst.field_frb();
    const auto rc = info_.mInst.field_rc();

    if (frt == frb && !rc)
        return;

    const Value res = e_.load_fpr(frb);

    if (frt != frb)
        e_.store_fpr(frt, res);

    e_.update_fpscr(rc);
}

CLHandler(fabs) {
    const auto frt = info_.mInst.field_frd();
    const auto frb = info_.mInst.field_frb();
    const auto rc = info_.mInst.field_rc();

    const Value res = e_.ins().fabs(e_.load_fpr(frb));

    e_.store_fpr(frt, res);

    if (rc)
        e_.copy_fpscr_to_cr1();
}
