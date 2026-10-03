#pragma once

#include "cl_util.h"

CLHandler(add) {
    const auto oe = info_.mInst.field_oe();
    const auto rc = info_.mInst.field_rc();
    const auto rd = info_.mInst.field_rd();
    const auto ra = info_.mInst.field_ra();
    const auto rb = info_.mInst.field_rb();

    const Value res = e_.ins().iadd(e_.load_gpr(ra), e_.load_gpr(rb));
    e_.store_gpr(rd, res);

    if (oe) {
        LOG_FATAL("add instruction OE bit not implemented.");
        assert(false);
    }

    if (res)
        e_.record_cr(0, res);
}

// TODO: the add with carry can be expressed with an (x86) adc, but as writing this,
// cranelift doesnt lower uadd_overflow_cin(), ARM also doesn't lower it
CLHandler(addze) {
    const auto oe = info_.mInst.field_oe();
    const auto rc = info_.mInst.field_rc();
    const auto rd = info_.mInst.field_rd();
    const auto ra = info_.mInst.field_ra();

    const Value source = e_.load_gpr(ra);
    const Value ca_in = e_.load_ca();

    const Value ca64 = e_.ins().uextend(types::I64(), ca_in);
    const Value res = e_.ins().iadd(source, ca64);

    e_.store_gpr(rd, res);

    if (oe) {
        LOG_FATAL("addze OE bit not implemented.");
        assert(false);
    } else {
        const Value source32 = e_.ins().ireduce(types::I32(), source);
        const Value ca32 = e_.ins().uextend(types::I32(), ca_in);

        const auto [unused, ca_out] = e_.ins().uadd_overflow(source32, ca32);
        e_.store_ca(ca_out);
    }

    if (rc)
        e_.record_cr(0, res);
}

CLHandler(addis) {
    const auto rt = info_.mInst.field_rd();
    const auto ra = info_.mInst.field_ra();
    const auto uimm = info_.mInst.field_uimm();

    const Value immediate = sign_extend<16>(uimm) << 16;
    Value toStore = e_.i64(immediate);

    // if 0 it's LI so it skips this
    if (ra != 0)
        toStore = e_.ins().iadd_imm_s(e_.load_gpr(ra), immediate);

    e_.store_gpr(rt, toStore);
}

CLHandler(addi) {
    const auto rt = info_.mInst.field_rd();
    const auto ra = info_.mInst.field_ra();
    const auto simm = info_.mInst.field_simm();

    const Value immediate = sign_extend<16>(simm);
    Value toStore = e_.i64(immediate);

    // if 0 it's LI so it skips this
    if (ra != 0)
        toStore = e_.ins().iadd_imm_s(e_.load_gpr(ra), immediate);

    e_.store_gpr(rt, toStore);
}

CLHandler(addic) {
    const auto rd = info_.mInst.field_rd();
    const auto ra = info_.mInst.field_ra();
    const auto simm = info_.mInst.field_simm();

    const Value source = e_.load_gpr(ra);
    const auto immediate = sign_extend<16>(simm);

    const Value res = e_.ins().iadd_imm_s(source, immediate);
    e_.store_gpr(rd, res);

    e_.store_ca(add_did_carry_imm32(e_, source, static_cast<uint32_t>(immediate)));

    if (info_.mInst.op == PpcOpcode::Addic_)
        e_.record_cr(0, res);
}

CLHandler(subfic) {
    const auto rd = info_.mInst.field_rd();
    const auto ra = info_.mInst.field_ra();
    const auto simm = info_.mInst.field_simm();

    const Value raV = e_.load_gpr(ra);
    const auto immediate = sign_extend<16>(simm);
    const Value res = e_.ins().isub(e_.i64(immediate), raV);
    e_.store_gpr(rd, res);

    e_.store_ca(sub_did_carry_imm32(e_, static_cast<uint32_t>(immediate), raV));
}

CLHandler(subf) {
    const auto oe = info_.mInst.field_oe();
    const auto rc = info_.mInst.field_rc();
    const auto rd = info_.mInst.field_rd();
    const auto ra = info_.mInst.field_ra();
    const auto rb = info_.mInst.field_rb();

    const Value res = e_.ins().isub(e_.load_gpr(rb), e_.load_gpr(ra));
    e_.store_gpr(rd, res);

    if (oe) {
        LOG_FATAL("addze OE bit not implemented.");
        assert(false);
    }

    if (rc)
        e_.record_cr(0, res);
}

CLHandler(subfe) {
    const auto oe = info_.mInst.field_oe();
    const auto rc = info_.mInst.field_rc();
    const auto rd = info_.mInst.field_rd();
    const auto ra = info_.mInst.field_ra();
    const auto rb = info_.mInst.field_rb();

    const Value raV = e_.load_gpr(ra);
    const Value rbV = e_.load_gpr(rb);
    const Value ca_in = e_.load_ca();

    // RT <- ~RA + RB + CA
    const Value not_ra = e_.ins().bnot(raV);

    const Value tmp = e_.ins().iadd(not_ra, rbV);
    const Value res = e_.ins().iadd(tmp, e_.ins().uextend(types::I64(), ca_in));

    e_.store_gpr(rd, res);

    if (oe) {
        LOG_FATAL("subfe OE bit not implemented.");
        assert(false);
    } else {
        e_.store_ca(sub_with_carry_did_carry32(e_, raV, rbV, ca_in));
    }

    if (rc)
        e_.record_cr(0, res);
}

CLHandler(and_) {
    const auto rs = info_.mInst.field_rs();
    const auto ra = info_.mInst.field_ra();
    const auto rb = info_.mInst.field_rb();
    const auto rc = info_.mInst.field_rc();

    const Value res = e_.ins().band(e_.load_gpr(rs), e_.load_gpr(rb));
    e_.store_gpr(ra, res);

    if (rc)
        e_.record_cr(0, res);
}

CLHandler(andi) {
    const auto ra = info_.mInst.field_ra();
    const auto rb = info_.mInst.field_rb();
    const auto uimm = info_.mInst.field_uimm();

    const Value res = e_.ins().band_imm_u(e_.load_gpr(rb), zero_extend<16>(uimm));
    e_.store_gpr(ra, res);
    e_.record_cr(0, res);
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

CLHandler(oris) {
    const auto ra = info_.mInst.field_ra();
    const auto rs = info_.mInst.field_rs();
    const auto uimm = info_.mInst.field_uimm();

    const Value immediate = zero_extend<16>(uimm) << 16;
    Value ored = e_.ins().bor_imm_u(e_.load_gpr(rs), immediate);
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

CLHandler(cmp) {
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

    e_.record_cr(crfd, lhs, rhs);
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

CLHandler(slw) {
    const auto ra = info_.mInst.field_ra();
    const auto rs = info_.mInst.field_rs();
    const auto rb = info_.mInst.field_rb();
    const auto rc = info_.mInst.field_rc();

    const Value word = e_.ins().ireduce(types::I32(), e_.load_gpr(rs));
    const Value count = e_.ins().ireduce(types::I32(), e_.load_gpr(rb));

    // cranelift automatically uses count & 31 for an I32 shift.
    const Value shifted = e_.ins().ishl(word, count);

    const Value out_of_range = e_.ins().band_imm_u(count, 32);
    const Value in_range = e_.ins().icmp_imm_u(IntCC::CL_INTCC_EQUAL, out_of_range, 0);
    const Value result32 = e_.ins().select(in_range, shifted, e_.i32(0));

    const Value result = e_.zext(types::I64(), result32);

    e_.store_gpr(ra, result);

    if (rc)
        e_.record_cr(0, result);
}

CLHandler(srawi) {
    const auto ra = info_.mInst.field_ra();
    const auto rs = info_.mInst.field_rs();
    const auto rc = info_.mInst.field_rc();
    const auto sh = info_.mInst.field_sh();

    const Value word = e_.ins().ireduce(types::I32(), e_.load_gpr(rs));
    const Value shifted = sh ? e_.ins().sshr_imm_u(word, sh) : word;
    const Value result = e_.sext(types::I64(), shifted);

    Value ca = e_.i8(0);

    if (sh) {
        const uint32_t discarded_mask = (uint32_t{1} << sh) - 1;
        const uint32_t ca_test_mask = 0x80000000u | discarded_mask;
        const Value tested = e_.ins().band_imm_u(word, ca_test_mask);

        ca = e_.ins().icmp_imm_u(IntCC::CL_INTCC_UNSIGNED_GREATER_THAN, tested, 0x80000000u);
    }

    e_.store_ca(ca);
    e_.store_gpr(ra, result);

    if (rc)
        e_.record_cr(0, result);
}

CLHandler(rlwimi) {
    const auto ra = info_.mInst.field_ra();
    const auto rs = info_.mInst.field_rs();
    const auto sh = info_.mInst.field_sh();
    const auto me = info_.mInst.field_me();
    const auto mb = info_.mInst.field_mb();
    const auto rc = info_.mInst.field_rc();

    const uint64_t mask = PPCMASK(mb + 32, me + 32);

    const Value old_ra = e_.load_gpr(ra);
    const Value word = e_.ins().ireduce(types::I32(), e_.load_gpr(rs));
    const Value rotated = sh ? e_.ins().rotl_imm_u(word, sh) : word;

    Value inserted = e_.zext(types::I64(), rotated);

    if (mask >> 32) {
        inserted = e_.ins().bor(inserted, e_.ins().ishl_imm_u(inserted, 32));
    }

    Value result;

    if (mask == UINT64_MAX) {
        result = inserted;
    } else {
        // result = (inserted & mask) | (old_ra & ~mask)
        result = e_.ins().bitselect(e_.i64(mask), inserted, old_ra);
    }

    e_.store_gpr(ra, result);

    if (rc)
        e_.record_cr(0, result);
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
        // no rotation and the mask only touches the low word:
        // operating directly on the GPR avoids the truncate/zext pair.
        if (sh == 0 && !(mask >> 32)) {
            return mask == UINT32_MAX ? e_.zext(types::I64(), e_.ins().ireduce(types::I32(), source)) :
                                        e_.ins().band(source, e_.i64(mask));
        }

        const Value word = e_.ins().ireduce(types::I32(), source);

        // if every bit that would wrap around is masked away,
        // rotation reduces to a simple left shift.
        if (InstrCheck_rlx_only_needs_low(sh, mask)) {
            return e_.zext(types::I64(), e_.ins().ishl_imm_u(word, sh));
        }

        const Value rotated = sh ? e_.ins().rotl_imm_u(word, sh) : word;

        // mask only touches the low 32 bits, so do the mask while
        // still operating on I32 and only then zero-extend.
        if (!(mask >> 32)) {
            const Value masked
                = mask == UINT32_MAX ? rotated : e_.ins().band(rotated, e_.i32(static_cast<uint32_t>(mask)));

            return e_.zext(types::I64(), masked);
        }

        // ROTL32 conceptually repeats the rotated word into both
        // halves of the 64-bit intermediate.
        const Value low = e_.zext(types::I64(), rotated);
        const Value repeated = e_.ins().bor(e_.ins().ishl_imm_u(low, 32), low);

        return mask == UINT64_MAX ? repeated : e_.ins().band(repeated, e_.i64(mask));
    }();

    e_.store_gpr(ra, result);

    if (rc)
        e_.record_cr(0, result, e_.i64(0));
}
