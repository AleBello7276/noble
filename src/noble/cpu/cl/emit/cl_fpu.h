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

CLHandler(fadd) {
    const auto frt = info_.mInst.field_frd();
    const auto fra = info_.mInst.field_fra();
    const auto frb = info_.mInst.field_frb();
    const auto rc = info_.mInst.field_rc();

    const Value res = e_.ins().fadd(e_.load_fpr(fra), e_.load_fpr(frb));

    e_.store_fpr(frt, res);
    e_.update_fpscr(rc);
}

CLHandler(fadds) {
    const auto frt = info_.mInst.field_frd();
    const auto fra = info_.mInst.field_fra();
    const auto frb = info_.mInst.field_frb();
    const auto rc = info_.mInst.field_rc();

    Value res = e_.ins().fadd(e_.load_fpr(fra), e_.load_fpr(frb));

    res = e_.to_single(res);

    e_.store_fpr(frt, res);
    e_.update_fpscr(rc);
}

CLHandler(fctidz) {
    const auto frt = info_.mInst.field_frd();
    const auto frb = info_.mInst.field_frb();
    const auto rc = info_.mInst.field_rc();

    const Value src = e_.load_fpr(frb);

    // explicitly round toward zero
    const Value rounded = e_.ins().trunc(src);
    const Value res = fctid_convert(e_, src, rounded);

    e_.store_fpr(frt, res);
    e_.update_fpscr(rc);
}

// TODO: optimise this
CLHandler(fctid) {
    const auto frt = info_.mInst.field_frd();
    const auto frb = info_.mInst.field_frb();
    const auto rc = info_.mInst.field_rc();

    const Value src = e_.load_fpr(frb);

    const Value rn = e_.ins().band_imm_u(e_.load_fpscr(), 0x3);

    // this is nasty
    const Value round_nearest = e_.ins().nearest(src);
    const Value round_zero = e_.ins().trunc(src);
    const Value round_positive = e_.ins().ceil(src);
    const Value round_negative = e_.ins().floor(src);

    Value rounded = round_nearest;

    rounded = e_.ins().select(e_.ins().icmp_imm_u(IntCC::CL_INTCC_EQUAL, rn, 1), round_zero, rounded);
    rounded = e_.ins().select(e_.ins().icmp_imm_u(IntCC::CL_INTCC_EQUAL, rn, 2), round_positive, rounded);
    rounded = e_.ins().select(e_.ins().icmp_imm_u(IntCC::CL_INTCC_EQUAL, rn, 3), round_negative, rounded);

    const Value res = fctid_convert(e_, src, rounded);

    e_.store_fpr(frt, res);
    e_.update_fpscr(rc);
}

CLHandler(fsub) {
    const auto frt = info_.mInst.field_frd();
    const auto fra = info_.mInst.field_fra();
    const auto frb = info_.mInst.field_frb();
    const auto rc = info_.mInst.field_rc();

    const Value res = e_.ins().fsub(e_.load_fpr(fra), e_.load_fpr(frb));

    e_.store_fpr(frt, res);
    e_.update_fpscr(rc);
}

CLHandler(fsubs) {
    const auto frt = info_.mInst.field_frd();
    const auto fra = info_.mInst.field_fra();
    const auto frb = info_.mInst.field_frb();
    const auto rc = info_.mInst.field_rc();

    Value res = e_.ins().fsub(e_.load_fpr(fra), e_.load_fpr(frb));

    res = e_.to_single(res);

    e_.store_fpr(frt, res);
    e_.update_fpscr(rc);
}

CLHandler(fnmsub) {
    const auto frt = info_.mInst.field_frd();
    const auto fra = info_.mInst.field_fra();
    const auto frb = info_.mInst.field_frb();
    const auto frc = info_.mInst.field_frc();
    const auto rc = info_.mInst.field_rc();

    const Value a = e_.load_fpr(fra);
    const Value b = e_.load_fpr(frb);
    const Value c = e_.load_fpr(frc);

    // -(a * c - b)
    // fma(a, c, -b) = a*c - b
    const Value sub = e_.ins().fma(a, c, e_.ins().fneg(b));
    const Value res = e_.ins().fneg(sub);

    e_.store_fpr(frt, res);
    e_.update_fpscr(rc);
}

CLHandler(fnmsubs) {
    const auto frt = info_.mInst.field_frd();
    const auto fra = info_.mInst.field_fra();
    const auto frb = info_.mInst.field_frb();
    const auto frc = info_.mInst.field_frc();
    const auto rc = info_.mInst.field_rc();

    const Value a = e_.load_fpr(fra);
    const Value b = e_.load_fpr(frb);
    const Value c = e_.load_fpr(frc);

    const Value sub = e_.ins().fma(a, c, e_.ins().fneg(b));
    Value res = e_.ins().fneg(sub);

    res = e_.to_single(res);

    e_.store_fpr(frt, res);
    e_.update_fpscr(rc);
}

CLHandler(fmadd) {
    const auto frt = info_.mInst.field_frd();
    const auto fra = info_.mInst.field_fra();
    const auto frb = info_.mInst.field_frb();
    const auto frc = info_.mInst.field_frc();
    const auto rc = info_.mInst.field_rc();

    const Value res = e_.ins().fma(e_.load_fpr(fra), e_.load_fpr(frc), e_.load_fpr(frb));

    e_.store_fpr(frt, res);
    e_.update_fpscr(rc);
}

CLHandler(fmadds) {
    const auto frt = info_.mInst.field_frd();
    const auto fra = info_.mInst.field_fra();
    const auto frb = info_.mInst.field_frb();
    const auto frc = info_.mInst.field_frc();
    const auto rc = info_.mInst.field_rc();

    Value res = e_.ins().fma(e_.load_fpr(fra), e_.load_fpr(frc), e_.load_fpr(frb));
    res = e_.to_single(res);

    e_.store_fpr(frt, res);
    e_.update_fpscr(rc);
}

CLHandler(fneg) {
    const auto frt = info_.mInst.field_frd();
    const auto frb = info_.mInst.field_frb();
    const auto rc = info_.mInst.field_rc();

    const Value res = e_.ins().fneg(e_.load_fpr(frb));
    e_.store_fpr(frt, res);

    // fneg does not alter FPSCR
    // Rc=1 copies FPSCR.{FX,FEX,VX,OX} to CR1
    if (rc)
        e_.copy_fpscr_to_cr1();
}

CLHandler(fcmpu) {
    const auto bf = info_.mInst.field_frb();
    const auto fra = info_.mInst.field_fra();
    const auto frb = info_.mInst.field_frb();

    const Value a = e_.load_fpr(fra);
    const Value b = e_.load_fpr(frb);

    const Value unordered = e_.ins().fcmp(FloatCC::CL_FLOATCC_UNORDERED, a, b);
    const Value lt = e_.ins().fcmp(FloatCC::CL_FLOATCC_LESS_THAN, a, b);
    const Value gt = e_.ins().fcmp(FloatCC::CL_FLOATCC_GREATER_THAN, a, b);

    const Value cr = e_.ins().select(
        unordered, e_.i32(1u << 24),
        e_.ins().select(lt, e_.i32(1u << 0), e_.ins().select(gt, e_.i32(1u << 8), e_.i32(1u << 16))));

    e_.ins().store(e_.builder.memflags_new(), cr, e_.vCpuState, CRFieldBitOffset(bf));

    // TODO: Accurate FPSCR compare semantics:
    //   - update FPCC
    //   - detect SNaN and set VXSNAN
    //   - fcmpo: handle VXVC / VE semantics
}

CLHandler(fcmpo) {
    const auto bf = info_.mInst.field_frb();
    const auto fra = info_.mInst.field_fra();
    const auto frb = info_.mInst.field_frb();

    const Value a = e_.load_fpr(fra);
    const Value b = e_.load_fpr(frb);

    const Value unordered = e_.ins().fcmp(FloatCC::CL_FLOATCC_UNORDERED, a, b);
    const Value lt = e_.ins().fcmp(FloatCC::CL_FLOATCC_LESS_THAN, a, b);
    const Value gt = e_.ins().fcmp(FloatCC::CL_FLOATCC_GREATER_THAN, a, b);

    const Value cr = e_.ins().select(
        unordered, e_.i32(1u << 24),
        e_.ins().select(lt, e_.i32(1u << 0), e_.ins().select(gt, e_.i32(1u << 8), e_.i32(1u << 16))));

    e_.ins().store(e_.builder.memflags_new(), cr, e_.vCpuState, CRFieldBitOffset(bf));

    // TODO: Accurate FPSCR compare semantics:
    //   - update FPCC
    //   - detect SNaN and set VXSNAN
    //   - fcmpo: handle VXVC / VE semantics
}

CLHandler(fsel) {
    const auto frt = info_.mInst.field_frd();
    const auto fra = info_.mInst.field_fra();
    const auto frb = info_.mInst.field_frb();
    const auto frc = info_.mInst.field_frc();
    const auto rc = info_.mInst.field_rc();

    const Value a = e_.load_fpr(fra);

    const Value ge_zero = e_.ins().fcmp(FloatCC::CL_FLOATCC_GREATER_THAN_OR_EQUAL, a, e_.f64(0.0));

    const Value res = e_.ins().select(ge_zero, e_.load_fpr(frc), e_.load_fpr(frb));

    e_.store_fpr(frt, res);
    e_.update_fpscr(rc);
}

CLHandler(fsqrt) {
    const auto frt = info_.mInst.field_frd();
    const auto frb = info_.mInst.field_frb();
    const auto rc = info_.mInst.field_rc();

    const Value res = e_.ins().sqrt(e_.load_fpr(frb));

    e_.store_fpr(frt, res);
    e_.update_fpscr(rc);
}

CLHandler(fsqrts) {
    const auto frt = info_.mInst.field_frd();
    const auto frb = info_.mInst.field_frb();
    const auto rc = info_.mInst.field_rc();

    Value res = e_.ins().sqrt(e_.load_fpr(frb));

    res = e_.to_single(res);

    e_.store_fpr(frt, res);
    e_.update_fpscr(rc);
}
