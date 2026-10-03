#pragma once

#include "cl_util.h"

CLHandler(add) {
    const Value lhs = e_.load_gpr(info_.mInst.field_ra());
    const Value rhs = e_.load_gpr(info_.mInst.field_rb());
    const Value result = e_.ins().iadd(lhs, rhs);

    if (info_.mInst.field_oe()) {
        const Value resultWord = e_.ins().ireduce(types::I32(), result);
        const Value lhsWord = e_.ins().ireduce(types::I32(), lhs);
        const Value rhsWord = e_.ins().ireduce(types::I32(), rhs);
        // signed word overflow occurs when both operands have a different sign from the result
        const Value signChanges
            = e_.ins().band(e_.ins().bxor(lhsWord, resultWord), e_.ins().bxor(rhsWord, resultWord));
        const Value overflow = e_.ins().icmp(IntCC::CL_INTCC_SIGNED_LESS_THAN, signChanges, e_.i32(0));
        constexpr int64_t ovMask = int64_t(1) << 30;
        const Value xer = e_.load_spr(eSPR::XER);
        const Value ov = e_.ins().select(overflow, e_.i64(ovMask), e_.i64(0));
        const Value updatedXer = e_.ins().bor(e_.ins().bor(e_.ins().band(xer, e_.i64(~ovMask)), ov),
                                              e_.ins().ishl(ov, e_.i64(1)));
        e_.store_spr(eSPR::XER, updatedXer);
    }

    e_.store_gpr(info_.mInst.field_rd(), result);
    if (info_.mInst.field_rc())
        e_.record_cr(0, result);
}

CLHandler(addis) {
    const auto rt = info_.mInst.field_rd();
    const auto ra = info_.mInst.field_ra();
    const auto uimm = info_.mInst.field_uimm();

    const auto shifted = sign_extend<16>(uimm) << 16;
    const Value immediate = e_.i64(shifted);
    Value toStore = immediate;

    // if 0 it's LI so it skips this
    if (ra != 0)
        toStore = e_.ins().iadd(e_.load_gpr(ra), immediate);

    e_.store_gpr(rt, toStore);
}

CLHandler(addi) {
    const auto rt = info_.mInst.field_rd();
    const auto ra = info_.mInst.field_ra();
    const auto uimm = info_.mInst.field_uimm();

    const Value immediate = e_.i64(sign_extend<16>(uimm));
    Value toStore = immediate;

    // if 0 it's LI so it skips this
    if (ra != 0)
        toStore = e_.ins().iadd(e_.load_gpr(ra), immediate);

    e_.store_gpr(rt, toStore);
}

CLHandler(addic) {
    const auto rt = info_.mInst.field_rd();
    const auto ra = info_.mInst.field_ra();
    const Value source = e_.load_gpr(ra);
    const Value immediate = e_.i64(sign_extend<16>(info_.mInst.field_uimm()));
    const Value result = e_.ins().iadd(source, immediate);

    // carry reflects the low word addition in the current guest execution mode
    const Value carry
        = e_.ins().icmp(IntCC::CL_INTCC_UNSIGNED_LESS_THAN, e_.ins().ireduce(types::I32(), result),
                        e_.ins().ireduce(types::I32(), source));
    constexpr int64_t caMask = int64_t(1) << 29;
    const Value xer = e_.load_spr(eSPR::XER);
    const Value ca = e_.ins().select(carry, e_.i64(caMask), e_.i64(0));
    e_.store_spr(eSPR::XER, e_.ins().bor(e_.ins().band(xer, e_.i64(~caMask)), ca));
    e_.store_gpr(rt, result);

    if (info_.mInst.op == PpcOpcode::Addic_)
        e_.record_cr(0, result);
}

CLHandler(subfe) {
    const auto rt = info_.mInst.field_rd();
    const Value lhs = e_.load_gpr(info_.mInst.field_ra());
    const Value rhs = e_.load_gpr(info_.mInst.field_rb());
    const Value xer = e_.load_spr(eSPR::XER);
    const Value carryIn = e_.ins().band(e_.ins().ushr(xer, e_.i64(29)), e_.i64(1));
    const Value result = e_.ins().iadd(e_.ins().iadd(e_.ins().bnot(lhs), rhs), carryIn);

    // carry is the absence of a borrow from the low word including the incoming carry
    const Value lhsWord = e_.ins().ireduce(types::I32(), lhs);
    const Value rhsWord = e_.ins().ireduce(types::I32(), rhs);
    const Value hasCarry = e_.ins().icmp(IntCC::CL_INTCC_NOT_EQUAL, carryIn, e_.i64(0));
    const Value carry = e_.ins().select(
        hasCarry, e_.ins().icmp(IntCC::CL_INTCC_UNSIGNED_GREATER_THAN_OR_EQUAL, rhsWord, lhsWord),
        e_.ins().icmp(IntCC::CL_INTCC_UNSIGNED_GREATER_THAN, rhsWord, lhsWord));
    constexpr int64_t caMask = int64_t(1) << 29;
    Value updatedXer = e_.ins().bor(e_.ins().band(xer, e_.i64(~caMask)),
                                    e_.ins().select(carry, e_.i64(caMask), e_.i64(0)));

    if (info_.mInst.field_oe()) {
        const Value resultWord = e_.ins().ireduce(types::I32(), result);
        const Value signChanges
            = e_.ins().band(e_.ins().bxor(rhsWord, lhsWord), e_.ins().bxor(rhsWord, resultWord));
        const Value overflow = e_.ins().icmp(IntCC::CL_INTCC_SIGNED_LESS_THAN, signChanges, e_.i32(0));
        constexpr int64_t ovMask = int64_t(1) << 30;
        const Value ov = e_.ins().select(overflow, e_.i64(ovMask), e_.i64(0));
        // ov reflects this operation and so remains set after any signed overflow
        updatedXer = e_.ins().bor(e_.ins().bor(e_.ins().band(updatedXer, e_.i64(~ovMask)), ov),
                                  e_.ins().ishl(ov, e_.i64(1)));
    }

    e_.store_spr(eSPR::XER, updatedXer);
    e_.store_gpr(rt, result);
    if (info_.mInst.field_rc())
        e_.record_cr(0, result);
}

CLHandler(or_) {
    const auto rc = info_.mInst.field_rc();
    const auto ra = info_.mInst.field_ra();
    const auto rs = info_.mInst.field_rs();
    const auto rb = info_.mInst.field_rb();

    // nop or yield (db16cyc)
    if (ra == rb && rb == rs && rc == 0) {
        // or r31, r31, r31 (db16cyc)
        if (info_.mInst.code == 0x7FFFFB78) {
            auto func_ref = e_.builder.declare_func_in_func(e_.jit, e_.backend->host_yield_id);
            e_.ins().call(func_ref);
            return;
        }

        // or acts as a nop (not needed to emit)
        e_.ins().nop();
        return;
    }

    Value res;

    if (rs == rb)  // Move Register (mr)
        res = e_.load_gpr(rs);
    else  // normal or operation
        res = e_.ins().bor(e_.load_gpr(rs), e_.load_gpr(rb));

    e_.store_gpr(ra, res);

    if (rc)
        e_.record_cr(0, res);
}

CLHandler(ori) {
    const auto ra = info_.mInst.field_ra();
    const auto rs = info_.mInst.field_rs();
    const auto uimm = info_.mInst.field_uimm();

    // nop -> ori 0, 0, 0
    if (ra == 0 && rs == 0 && uimm == 0) {
        e_.ins().nop();
        return;
    }

    const Value immediate = e_.i64(zero_extend<16>(uimm));
    Value ored = e_.ins().bor(e_.load_gpr(rs), immediate);

    e_.store_gpr(ra, ored);
}

CLHandler(cmpi) {
    const auto l = info_.mInst.field_l();
    const auto ra = info_.mInst.field_ra();
    const auto simm = info_.mInst.field_simm();
    const auto crfd = info_.mInst.field_crfd();

    Value lhs;
    Value rhs;

    if (l) {
        lhs = e_.load_gpr(ra);
        rhs = e_.i64(sign_extend<16>(simm));
    } else {
        lhs = e_.ins().ireduce(types::I32(), e_.load_gpr(ra));
        rhs = e_.i32(sign_extend<16>(simm));
    }

    e_.record_cr(crfd, lhs, rhs);
}

CLHandler(cmpli) {
    const auto l = info_.mInst.field_l();
    const auto ra = info_.mInst.field_ra();
    const auto simm = info_.mInst.field_simm();
    const auto crfd = info_.mInst.field_crfd();

    Value lhs;
    Value rhs;

    if (l) {
        lhs = e_.load_gpr(ra);
        rhs = e_.i64(sign_extend<16>(simm));
    } else {
        lhs = e_.ins().ireduce(types::I32(), e_.load_gpr(ra));
        rhs = e_.i32(sign_extend<16>(simm));
    }

    e_.record_cr<false>(crfd, lhs, rhs);
}

CLHandler(cmpl) {
    const auto l = info_.mInst.field_l();
    const auto ra = info_.mInst.field_ra();
    const auto rb = info_.mInst.field_rb();
    const auto crfd = info_.mInst.field_crfd();

    Value lhs;
    Value rhs;

    if (l) {
        lhs = e_.load_gpr(ra);
        rhs = e_.load_gpr(rb);
    } else {
        lhs = e_.ins().ireduce(types::I32(), e_.load_gpr(ra));
        rhs = e_.ins().ireduce(types::I32(), e_.load_gpr(rb));
    }

    e_.record_cr<false>(crfd, lhs, rhs);
}

CLHandler(extsb) {
    const auto ra = info_.mInst.field_ra();
    const auto rs = info_.mInst.field_rs();
    const auto rc = info_.mInst.field_rc();

    Value res = e_.sext(types::I64(), e_.ins().ireduce(types::I8(), e_.load_gpr(rs)));
    e_.store_gpr(ra, res);

    if (rc)
        e_.record_cr(0, res);
}

CLHandler(cntlzw) {
    const auto ra = info_.mInst.field_ra();
    const auto rs = info_.mInst.field_rs();
    const auto rc = info_.mInst.field_rc();

    Value count = e_.ins().clz(e_.ins().ireduce(types::I32(), e_.load_gpr(rs)));
    Value ext = e_.zext(types::I64(), count);
    e_.store_gpr(ra, ext);

    if (rc)
        e_.record_cr(0, ext);
}

CLHandler(rlwinm) {
    const auto ra = info_.mInst.field_ra();
    const auto rs = info_.mInst.field_rs();
    const auto rc = info_.mInst.field_rc();
    const auto sh = info_.mInst.field_sh();
    const auto mb = info_.mInst.field_mb();
    const auto me = info_.mInst.field_me();

    const uint64_t mask = PPCMASK(mb + 32, me + 32);
    const Value source = e_.load_gpr(rs);

    const Value result = [&]() -> Value {
        // a nonwrapping mask with no rotation can operate directly on the full gpr
        if (mask <= UINT32_MAX && sh == 0)
            return e_.ins().band(source, e_.i64(mask));

        const Value word = e_.ins().ireduce(types::I32(), source);

        // the mask discards every wrapped bit so a word shift and zero extension suffice
        if (InstrCheck_rlx_only_needs_low(sh, mask))
            return e_.zext(types::I64(), e_.ins().ishl(word, e_.i32(sh)));

        const Value rotated = sh ? e_.ins().rotl(word, e_.i32(sh)) : word;

        // use a word mask before zero extending the result to the gpr width
        if (mask <= UINT32_MAX) {
            const Value masked = mask == UINT32_MAX ? rotated : e_.ins().band(rotated, e_.i32(mask));
            return e_.zext(types::I64(), masked);
        }

        // wrapping masks include the upper word of the architectural repeated rotation
        const Value low = e_.zext(types::I64(), rotated);
        const Value repeated = e_.ins().bor(e_.ins().ishl(low, e_.i64(32)), low);
        return mask == UINT64_MAX ? repeated : e_.ins().band(repeated, e_.i64(mask));
    }();

    e_.store_gpr(ra, result);

    if (rc)
        e_.record_cr(0, result, e_.i64(0));
}
