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

    if (rc)
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

    const auto immediate = sign_extend<16>(uimm) << 16;
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

    const auto immediate = sign_extend<16>(simm);
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

CLHandler(subfc) {
    const auto rd = info_.mInst.field_rd();
    const auto ra = info_.mInst.field_ra();
    const auto rb = info_.mInst.field_rb();
    const auto oe = info_.mInst.field_oe();
    const auto rc = info_.mInst.field_rc();

    const Value ra_v = e_.load_gpr(ra);
    const Value rb_v = e_.load_gpr(rb);

    // RT = RB - RA
    const Value result = e_.ins().isub(rb_v, ra_v);

    e_.store_gpr(rd, result);

    const Value ca
        = e_.ins().icmp(IntCC::CL_INTCC_UNSIGNED_GREATER_THAN_OR_EQUAL, e_.reduce32(rb_v), e_.reduce32(ra_v));

    e_.store_ca(ca);

    if (oe) {
        LOG_FATAL("subfc OE bit not implemented.");
        assert(false);
    }

    if (rc)
        e_.record_cr(0, result);
}

CLHandler(neg) {
    const auto oe = info_.mInst.field_oe();
    const auto rc = info_.mInst.field_rc();
    const auto rd = info_.mInst.field_rd();
    const auto ra = info_.mInst.field_ra();

    const Value source = e_.load_gpr(ra);

    // RT <- ~RA + 1
    const Value result = e_.ins().ineg(source);
    e_.store_gpr(rd, result);

    // TODO: handle OV
    if (oe) {
        LOG_FATAL("neg OE bit not implemented.");
        throw std::runtime_error("neg OE bit not implemented.");
        assert(false);
    }

    if (rc)
        e_.record_cr(0, result);
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

CLHandler(andc) {
    const auto rs = info_.mInst.field_rs();
    const auto ra = info_.mInst.field_ra();
    const auto rb = info_.mInst.field_rb();
    const auto rc = info_.mInst.field_rc();

    // cranelift has band_not which is equivalent to this, it generates the same bnot and band
    const Value res = e_.ins().band(e_.load_gpr(rs), e_.ins().bnot(e_.load_gpr(rb)));
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

CLHandler(orc) {
    const auto ra = info_.mInst.field_ra();
    const auto rs = info_.mInst.field_rs();
    const auto rb = info_.mInst.field_rb();
    const auto rc = info_.mInst.field_rc();

    const Value result = e_.ins().bor(e_.load_gpr(rs), e_.ins().bnot(e_.load_gpr(rb)));

    e_.store_gpr(ra, result);

    if (rc)
        e_.record_cr(0, result);
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

    const auto immediate = zero_extend<16>(uimm) << 16;
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

    Value res = e_.sext64(e_.ins().ireduce(types::I8(), e_.load_gpr(rs)));
    e_.store_gpr(ra, res);

    if (rc)
        e_.record_cr(0, res);
}

CLHandler(extsw) {
    const auto ra = info_.mInst.field_ra();
    const auto rs = info_.mInst.field_rs();
    const auto rc = info_.mInst.field_rc();

    const Value res = e_.sext64(e_.reduce32(e_.load_gpr(rs)));

    e_.store_gpr(ra, res);

    if (rc)
        e_.record_cr(0, res);
}

CLHandler(cntlzw) {
    const auto ra = info_.mInst.field_ra();
    const auto rs = info_.mInst.field_rs();
    const auto rc = info_.mInst.field_rc();

    Value count = e_.ins().clz(e_.ins().ireduce(types::I32(), e_.load_gpr(rs)));
    Value ext = e_.zext64(count);
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

    const Value result = e_.zext64(result32);

    e_.store_gpr(ra, result);

    if (rc)
        e_.record_cr(0, result);
}

CLHandler(srw) {
    const auto ra = info_.mInst.field_ra();
    const auto rs = info_.mInst.field_rs();
    const auto rb = info_.mInst.field_rb();
    const auto rc = info_.mInst.field_rc();

    const Value word = e_.reduce32(e_.load_gpr(rs));
    const Value count = e_.reduce32(e_.load_gpr(rb));

    const Value shifted = e_.ins().ushr(word, count);
    const Value out_of_range = e_.ins().band_imm_u(count, 32);
    const Value in_range = e_.ins().icmp_imm_u(IntCC::CL_INTCC_EQUAL, out_of_range, 0);
    const Value result = e_.zext64(e_.ins().select(in_range, shifted, e_.i32(0)));

    e_.store_gpr(ra, result);

    if (rc)
        e_.record_cr(0, result);
}

CLHandler(srd) {
    const auto ra = info_.mInst.field_ra();
    const auto rs = info_.mInst.field_rs();
    const auto rb = info_.mInst.field_rb();
    const auto rc = info_.mInst.field_rc();

    const Value source = e_.load_gpr(rs);
    const Value count = e_.ins().ireduce(types::I8(), e_.load_gpr(rb));
    const Value sh = e_.ins().band_imm_u(count, 0x7F);

    const Value shifted = e_.ins().ushr(source, sh);
    const Value out_of_range = e_.ins().band_imm_u(sh, 0x40);
    const Value in_range = e_.ins().icmp_imm_u(IntCC::CL_INTCC_EQUAL, out_of_range, 0);

    const Value result = e_.ins().select(in_range, shifted, e_.i64(0));

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
    const Value result = e_.sext64(shifted);

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

    Value inserted = e_.zext64(rotated);

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
            return mask == UINT32_MAX ? e_.zext64(e_.ins().ireduce(types::I32(), source)) :
                                        e_.ins().band(source, e_.i64(mask));
        }

        const Value word = e_.ins().ireduce(types::I32(), source);

        // if every bit that would wrap around is masked away,
        // rotation reduces to a simple left shift.
        if (InstrCheck_rlx_only_needs_low(sh, mask)) {
            return e_.zext64(e_.ins().ishl_imm_u(word, sh));
        }

        const Value rotated = sh ? e_.ins().rotl_imm_u(word, sh) : word;

        // mask only touches the low 32 bits, so do the mask while
        // still operating on I32 and only then zero-extend.
        if (!(mask >> 32)) {
            const Value masked
                = mask == UINT32_MAX ? rotated : e_.ins().band(rotated, e_.i32(static_cast<uint32_t>(mask)));

            return e_.zext64(masked);
        }

        // ROTL32 conceptually repeats the rotated word into both
        // halves of the 64-bit intermediate.
        const Value low = e_.zext64(rotated);
        const Value repeated = e_.ins().bor(e_.ins().ishl_imm_u(low, 32), low);

        return mask == UINT64_MAX ? repeated : e_.ins().band(repeated, e_.i64(mask));
    }();

    e_.store_gpr(ra, result);

    if (rc)
        e_.record_cr(0, result, e_.i64(0));
}

CLHandler(rldicl) {
    const auto ra = info_.mInst.field_ra();
    const auto rs = info_.mInst.field_rs();
    const auto rc = info_.mInst.field_rc();
    const auto sh = info_.mInst.field_sh();
    const auto mb = info_.mInst.field_mb();

    const uint64_t mask = PPCMASK(mb, 63);
    const Value source = e_.load_gpr(rs);

    const Value result = [&]() -> Value {
        // srdi RA,RS,n
        // the rotate+mask is just a right shift
        if (mb && sh == 64 - mb)
            return e_.ins().ushr_imm_u(source, mb);

        // No rotation.
        if (sh == 0)
            return mask == UINT64_MAX ? source : e_.ins().band(source, e_.i64(mask));

        const Value rotated = e_.ins().rotl_imm_u(source, sh);

        return mask == UINT64_MAX ? rotated : e_.ins().band(rotated, e_.i64(mask));
    }();

    e_.store_gpr(ra, result);

    if (rc)
        e_.record_cr(0, result);
}

CLHandler(rldicr) {
    const auto ra = info_.mInst.field_ra();
    const auto rs = info_.mInst.field_rs();
    const auto rc = info_.mInst.field_rc();
    const auto sh = info_.mInst.field_sh();
    const auto me = info_.mInst.field_mb();

    const uint64_t mask = PPCMASK(0, me);
    const Value source = e_.load_gpr(rs);

    const Value result = [&]() -> Value {
        // sldi RA,RS,n
        if (me == 63 - sh)
            return sh ? e_.ins().ishl_imm_u(source, sh) : source;

        if (sh == 0)
            return mask == UINT64_MAX ? source : e_.ins().band(source, e_.i64(mask));

        const Value rotated = e_.ins().rotl_imm_u(source, sh);

        return mask == UINT64_MAX ? rotated : e_.ins().band(rotated, e_.i64(mask));
    }();

    e_.store_gpr(ra, result);

    if (rc)
        e_.record_cr(0, result);
}

CLHandler(divwu) {
    const auto oe = info_.mInst.field_oe();
    const auto rc = info_.mInst.field_rc();
    const auto rd = info_.mInst.field_rd();
    const auto ra = info_.mInst.field_ra();
    const auto rb = info_.mInst.field_rb();

    if (oe) {
        LOG_FATAL("divwu instruction OE bit not implemented.");
        assert(false);
    }

    const Value dividend = e_.reduce32(e_.load_gpr(ra));
    const Value divisor = e_.reduce32(e_.load_gpr(rb));

    // TODO: ISA for divide by 0 assumes undefined, eventually i should
    // check what real hardware "defines" as undefined when this happens,
    // if games do not abuse the undefined beheviour making quotient 0 should be enough
    // so cranelift udiv never traps
    const Value is_zero = e_.ins().icmp_imm_u(IntCC::CL_INTCC_EQUAL, divisor, 0);

    // if divisor == 0,
    // dividend -> 0
    // divisor -> 1
    // 0 / 1 = 0
    const Value safe_dividend = e_.ins().select(is_zero, e_.i32(0), dividend);
    const Value safe_divisor = e_.ins().select(is_zero, e_.i32(1), divisor);

    const Value quotient = e_.ins().udiv(safe_dividend, safe_divisor);
    const Value result = e_.zext64(quotient);

    e_.store_gpr(rd, result);

    /*
        theres also another way to naturally make
        dividend 1 and divisor 0 if divisor == 0
        this should be benchmarked against the *select* can be written like this:

        const Value zero32 =
        e_.ins().uextend(types::I32(), is_zero);

        // divisor == 0:  0 | 1 = 1
        // divisor != 0:  x | 0 = x
        const Value safe_divisor =
        e_.ins().bor(divisor, zero32);

        // divisor == 0:  1 - 1 = 0x00000000
        // divisor != 0:  0 - 1 = 0xFFFFFFFF
        const Value dividend_mask =
        e_.ins().iadd_imm_s(zero32, -1);

        const Value safe_dividend =
        e_.ins().band(dividend, dividend_mask);
    */

    if (rc)
        e_.record_cr(0, result);
}

CLHandler(mullw) {
    const auto oe = info_.mInst.field_oe();
    const auto rc = info_.mInst.field_rc();
    const auto rd = info_.mInst.field_rd();
    const auto ra = info_.mInst.field_ra();
    const auto rb = info_.mInst.field_rb();

    if (oe) {
        LOG_FATAL("mullw instruction OE bit not implemented.");
        assert(false);
    }

    const Value a32 = e_.reduce32(e_.load_gpr(ra));
    const Value b32 = e_.reduce32(e_.load_gpr(rb));

    const Value a64 = e_.sext64(a32);
    const Value b64 = e_.sext64(b32);

    const Value res = e_.ins().imul(a64, b64);
    e_.store_gpr(rd, res);

    if (rc)
        e_.record_cr(0, res);
}

CLHandler(mulli) {
    const auto rd = info_.mInst.field_rd();
    const auto ra = info_.mInst.field_ra();
    const auto simm = info_.mInst.field_simm();

    const auto immediate = sign_extend<16>(simm);

    Value res = e_.ins().imul_imm_s(e_.load_gpr(ra), immediate);
    e_.store_gpr(rd, res);
}

CLHandler(xor_) {
    const auto rc = info_.mInst.field_rc();
    const auto rs = info_.mInst.field_rs();
    const auto ra = info_.mInst.field_ra();
    const auto rb = info_.mInst.field_rb();

    const Value res = e_.ins().bxor(e_.load_gpr(rs), e_.load_gpr(rb));
    e_.store_gpr(ra, res);

    if (rc)
        e_.record_cr(0, res);
}

CLHandler(xori) {
    const auto ra = info_.mInst.field_ra();
    const auto rs = info_.mInst.field_rs();
    const auto uimm = info_.mInst.field_uimm();

    const Value res = e_.ins().bxor(e_.load_gpr(rs), e_.i64(zero_extend<16>(uimm)));

    e_.store_gpr(ra, res);
}

CLHandler(xoris) {
    const auto ra = info_.mInst.field_ra();
    const auto rs = info_.mInst.field_rs();
    const auto uimm = info_.mInst.field_uimm();

    const Value res = e_.ins().bxor(e_.load_gpr(rs), e_.i64(zero_extend<16>(uimm) << 16));

    e_.store_gpr(ra, res);
}

CLHandler(nor) {
    const auto ra = info_.mInst.field_ra();
    const auto rs = info_.mInst.field_rs();
    const auto rb = info_.mInst.field_rb();
    const auto rc = info_.mInst.field_rc();

    const Value rsV = e_.load_gpr(rs);

    Value res;

    // ~(x | x) == ~x
    if (rs == rb) {
        res = e_.ins().bnot(rsV);
    } else {
        // only load when rs != rb, cranelift probably would optimise the double load anyway
        const Value rbV = e_.load_gpr(rb);
        res = e_.ins().bnot(e_.ins().bor(rsV, rbV));
    }

    e_.store_gpr(ra, res);

    if (rc)
        e_.record_cr(0, res);
}

CLHandler(eqv) {
    const auto ra = info_.mInst.field_ra();
    const auto rs = info_.mInst.field_rs();
    const auto rb = info_.mInst.field_rb();
    const auto rc = info_.mInst.field_rc();

    const Value res = e_.ins().bnot(e_.ins().bxor(e_.load_gpr(rs), e_.load_gpr(rb)));

    e_.store_gpr(ra, res);

    if (rc)
        e_.record_cr(0, res);
}
