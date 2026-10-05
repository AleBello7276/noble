#pragma once

#include "cl_util.h"

inline void emit_stvx(EmitterContext& e_, uint32_t vs, uint32_t ra, uint32_t rb) {
    const Value base = ra ? e_.load_gpr(ra) : e_.i64(0);
    const Value ea = e_.ins().band_imm_u(e_.ins().iadd(base, e_.load_gpr(rb)), ~0xFULL);

    const Value value = e_.byteswap_v128(e_.load_vr(vs, types::I8X16()));

    e_.store_memory(ea, value);
}

CLHandler(stvx) {
    emit_stvx(e_, info_.mInst.field_vs(), info_.mInst.field_ra(), info_.mInst.field_rb());
}

CLHandler(stvxl) {
    emit_stvx(e_, info_.mInst.field_vs(), info_.mInst.field_ra(), info_.mInst.field_rb());
}

CLHandler(stvx128) {
    emit_stvx(e_, info_.mInst.field_vds128(), info_.mInst.field_ra(), info_.mInst.field_rb());
}

CLHandler(stvxl128) {
    emit_stvx(e_, info_.mInst.field_vds128(), info_.mInst.field_ra(), info_.mInst.field_rb());
}

inline void emit_vspltisw(EmitterContext& e_, uint32_t vd, uint32_t uimm) {
    const int32_t simm = static_cast<int32_t>(sign_extend<5>(uimm));
    const Value value = e_.ins().splat(types::I32X4(), e_.i32(simm));

    e_.store_vr(vd, value);
}

CLHandler(vspltisw) {
    emit_vspltisw(e_, info_.mInst.field_vd(), info_.mInst.field_vsimm());
}

CLHandler(vspltisw128) {
    emit_vspltisw(e_, info_.mInst.field_vds128(), info_.mInst.field_vsimm());
}

//
//
//

inline Value unpack_overflow_nan(EmitterContext& e_, Value value, uint32_t overflow) {
    const Value overflow_v = e_.ins().splat(types::I32X4(), e_.i32(overflow));
    const Value qnan = e_.ins().splat(types::I32X4(), e_.i32(0x7FC00000u));
    const Value mask = e_.ins().icmp(IntCC::CL_INTCC_EQUAL, value, overflow_v);

    return e_.ins().bitselect(mask, qnan, value);
}

inline Value half_to_f32(EmitterContext& e_, Value half) {
    const Value h = e_.ins().uextend(types::I32(), half);

    const Value sign = e_.ins().ishl_imm_u(e_.ins().band_imm_u(h, 0x8000), 16);
    const Value exp = e_.ins().band_imm_u(e_.ins().ushr_imm_u(h, 10), 0x1F);
    const Value mant = e_.ins().band_imm_u(h, 0x3FF);

    const Value normal_exp = e_.ins().iadd_imm_u(exp, 112);
    const Value normal = e_.ins().bor(
        sign, e_.ins().bor(e_.ins().ishl_imm_u(normal_exp, 23), e_.ins().ishl_imm_u(mant, 13)));

    const Value inf_nan
        = e_.ins().bor(sign, e_.ins().bor(e_.i32(0x7F800000u), e_.ins().ishl_imm_u(mant, 13)));

    const Value shift = e_.ins().isub(e_.ins().clz(mant), e_.i32(21));
    const Value sub_exp = e_.ins().isub(e_.i32(113), shift);
    const Value sub_mant = e_.ins().band_imm_u(e_.ins().ishl(mant, shift), 0x3FF);

    const Value subnormal = e_.ins().bor(
        sign, e_.ins().bor(e_.ins().ishl_imm_u(sub_exp, 23), e_.ins().ishl_imm_u(sub_mant, 13)));

    const Value exp_zero = e_.ins().icmp_imm_u(IntCC::CL_INTCC_EQUAL, exp, 0);
    const Value exp_inf = e_.ins().icmp_imm_u(IntCC::CL_INTCC_EQUAL, exp, 0x1F);
    const Value mant_zero = e_.ins().icmp_imm_u(IntCC::CL_INTCC_EQUAL, mant, 0);

    const Value zero_or_sub = e_.ins().select(mant_zero, sign, subnormal);

    Value bits = e_.ins().select(exp_inf, inf_nan, normal);
    bits = e_.ins().select(exp_zero, zero_or_sub, bits);

    return e_.ins().bitcast(types::F32(), MemFlags{}, bits);
}

inline Value emit_vupkd3d(EmitterContext& e_, Value source, uint32_t type) {
    const Value zero = e_.ins().splat(types::I8X16(), e_.i8(0));

    switch (type) {
    case 0: {
        static constexpr std::array<uint8_t, 16> mask = {
            14, 16, 16, 16, 13, 16, 16, 16, 12, 16, 16, 16, 15, 16, 16, 16,
        };

        Value value = e_.ins().shuffle(source, zero, mask);
        value = e_.ins().bitcast(types::I32X4(), MemFlags{}, value);

        return e_.ins().bor(value, e_.ins().splat(types::I32X4(), e_.i32(0x3F800000u)));
    }

    case 1: {
        static constexpr std::array<uint8_t, 16> mask = {
            14, 15, 16, 16, 12, 13, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
        };

        Value value = e_.ins().shuffle(source, zero, mask);
        value = e_.ins().bitcast(types::I32X4(), MemFlags{}, value);
        value = e_.ins().sshr_imm_u(e_.ins().ishl_imm_u(value, 16), 16);

        const Value base
            = make_i32x4(e_, e_.i32(0x40400000u), e_.i32(0x40400000u), e_.i32(0), e_.i32(0x3F800000u));

        value = e_.ins().iadd(value, base);

        return unpack_overflow_nan(e_, value, 0x403F8000u);
    }

    case 2: {
        const Value words = e_.ins().bitcast(types::I32X4(), MemFlags{}, source);
        const Value packed = e_.ins().extractlane(words, 3);

        Value x = e_.ins().band_imm_u(packed, 0x3FF);
        Value y = e_.ins().band_imm_u(e_.ins().ushr_imm_u(packed, 10), 0x3FF);
        Value z = e_.ins().band_imm_u(e_.ins().ushr_imm_u(packed, 20), 0x3FF);
        const Value w = e_.ins().ushr_imm_u(packed, 30);

        x = e_.ins().sshr_imm_u(e_.ins().ishl_imm_u(x, 22), 22);
        y = e_.ins().sshr_imm_u(e_.ins().ishl_imm_u(y, 22), 22);
        z = e_.ins().sshr_imm_u(e_.ins().ishl_imm_u(z, 22), 22);

        Value value = make_i32x4(e_, x, y, z, w);

        const Value base = make_i32x4(e_, e_.i32(0x40400000u), e_.i32(0x40400000u), e_.i32(0x40400000u),
                                      e_.i32(0x3F800000u));

        value = e_.ins().iadd(value, base);

        return unpack_overflow_nan(e_, value, 0x403FFE00u);
    }

    case 3: {
        static constexpr std::array<uint8_t, 16> mask = {
            14, 15, 12, 13, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
        };

        const Value shuffled = e_.ins().shuffle(source, zero, mask);
        const Value halves = e_.ins().bitcast(types::I16X8(), MemFlags{}, shuffled);

        const Value x = half_to_f32(e_, e_.ins().extractlane(halves, 0));
        const Value y = half_to_f32(e_, e_.ins().extractlane(halves, 1));

        return make_f32x4(e_, x, y, e_.f32(0.0f), e_.f32(1.0f));
    }

    case 4: {
        static constexpr std::array<uint8_t, 16> mask = {
            10, 11, 16, 16, 8, 9, 16, 16, 14, 15, 16, 16, 12, 13, 16, 16,
        };

        Value value = e_.ins().shuffle(source, zero, mask);
        value = e_.ins().bitcast(types::I32X4(), MemFlags{}, value);
        value = e_.ins().sshr_imm_u(e_.ins().ishl_imm_u(value, 16), 16);
        value = e_.ins().iadd(value, e_.ins().splat(types::I32X4(), e_.i32(0x40400000u)));

        return unpack_overflow_nan(e_, value, 0x403F8000u);
    }

    case 5: {
        static constexpr std::array<uint8_t, 16> mask = {
            10, 11, 8, 9, 14, 15, 12, 13, 16, 16, 16, 16, 16, 16, 16, 16,
        };

        const Value shuffled = e_.ins().shuffle(source, zero, mask);
        const Value halves = e_.ins().bitcast(types::I16X8(), MemFlags{}, shuffled);

        return make_f32x4(e_, half_to_f32(e_, e_.ins().extractlane(halves, 0)),
                          half_to_f32(e_, e_.ins().extractlane(halves, 1)),
                          half_to_f32(e_, e_.ins().extractlane(halves, 2)),
                          half_to_f32(e_, e_.ins().extractlane(halves, 3)));
    }

    case 6: {
        static constexpr std::array<uint8_t, 16> mask = {
            12, 13, 14, 16, 9, 10, 11, 16, 14, 15, 8, 16, 11, 16, 16, 16,
        };

        Value value = e_.ins().shuffle(source, zero, mask);
        value = e_.ins().bitcast(types::I32X4(), MemFlags{}, value);

        const Value shifted = e_.ins().ushr_imm_u(value, 4);

        value = make_i32x4(e_, e_.ins().extractlane(value, 0), e_.ins().extractlane(shifted, 2),
                           e_.ins().extractlane(value, 1), e_.ins().extractlane(shifted, 3));

        value = e_.ins().sshr_imm_u(e_.ins().ishl_imm_u(value, 12), 12);

        const Value base = make_i32x4(e_, e_.i32(0x40400000u), e_.i32(0x40400000u), e_.i32(0x40400000u),
                                      e_.i32(0x3F800000u));

        value = e_.ins().iadd(value, base);

        return unpack_overflow_nan(e_, value, 0x40380000u);
    }

    default:
        assert(false);
        return zero;
    }
}

// TODO: refactor the whole thing above
CLHandler(vupkd3d128) {
    const auto vd = info_.mInst.field_vds128();
    const auto vb = info_.mInst.field_vb128();
    const auto type = info_.mInst.field_vuimm();

    const Value source = e_.load_vr(vb, types::I8X16());
    const Value result = emit_vupkd3d(e_, source, type);

    e_.store_vr(vd, result);
}

inline void emit_lvx(EmitterContext& e_, uint32_t vd, uint32_t ra, uint32_t rb) {
    const Value base = ra ? e_.load_gpr(ra) : e_.i64(0);
    const Value ea = e_.ins().band_imm_u(e_.ins().iadd(base, e_.load_gpr(rb)), ~0xFULL);

    const Value value = e_.byteswap_v128(e_.load_memory(ea, types::I8X16()));

    e_.store_vr(vd, value);
}

CLHandler(lvx) {
    emit_lvx(e_, info_.mInst.field_vd(), info_.mInst.field_ra(), info_.mInst.field_rb());
}

CLHandler(lvxl) {
    emit_lvx(e_, info_.mInst.field_vd(), info_.mInst.field_ra(), info_.mInst.field_rb());
}

CLHandler(lvx128) {
    emit_lvx(e_, info_.mInst.field_vds128(), info_.mInst.field_ra(), info_.mInst.field_rb());
}

CLHandler(lvxl128) {
    emit_lvx(e_, info_.mInst.field_vds128(), info_.mInst.field_ra(), info_.mInst.field_rb());
}

CLHandler(vpermwi128) {
    const auto vd = info_.mInst.field_vds128();
    const auto vb = info_.mInst.field_vb128();
    const auto uimm = info_.mInst.field_perm();

    const auto mask = make_word_shuffle_mask((uimm >> 6) & 3, (uimm >> 4) & 3, (uimm >> 2) & 3, uimm & 3);

    const Value source = e_.load_vr(vb, types::I8X16());
    const Value result = e_.ins().shuffle(source, source, mask);

    e_.store_vr(vd, result);
}

inline void emit_vsubfp(EmitterContext& e_, uint32_t vd, uint32_t va, uint32_t vb) {
    const Value a = e_.load_vr(va, types::F32X4());
    const Value b = e_.load_vr(vb, types::F32X4());

    const Value result = e_.ins().fsub(a, b);

    e_.store_vr(vd, result);
}

CLHandler(vsubfp) {
    emit_vsubfp(e_, info_.mInst.field_vd(), info_.mInst.field_va(), info_.mInst.field_vb());
}

CLHandler(vsubfp128) {
    emit_vsubfp(e_, info_.mInst.field_vds128(), info_.mInst.field_va128(), info_.mInst.field_vb128());
}

inline void emit_vor(EmitterContext& e_, uint32_t vd, uint32_t va, uint32_t vb) {
    const Value a = e_.load_vr(va, types::I8X16());

    if (va == vb) {
        if (vd != va)
            e_.store_vr(vd, a);

        return;
    }

    const Value result = e_.ins().bor(a, e_.load_vr(vb, types::I8X16()));

    e_.store_vr(vd, result);
}

CLHandler(vor) {
    emit_vor(e_, info_.mInst.field_vd(), info_.mInst.field_va(), info_.mInst.field_vb());
}

CLHandler(vor128) {
    emit_vor(e_, info_.mInst.field_vds128(), info_.mInst.field_va128(), info_.mInst.field_vb128());
}

CLHandler(vrlimi128) {
    const auto vd = info_.mInst.field_vds128();
    const auto vb = info_.mInst.field_vb128();
    const auto imm = info_.mInst.field_vuimm();
    const auto rotate = info_.mInst.field_zimm();

    std::array<uint8_t, 16> mask{};

    for (uint32_t i = 0; i < 4; ++i) {
        const bool replace = (imm >> (3 - i)) & 1;
        const uint32_t lane = replace ? (i + rotate) & 3 : i;
        const uint32_t base = lane * 4 + (replace ? 0 : 16);

        for (uint32_t j = 0; j < 4; ++j)
            mask[i * 4 + j] = static_cast<uint8_t>(base + j);
    }

    const Value source = e_.load_vr(vb, types::I8X16());
    const Value dest = e_.load_vr(vd, types::I8X16());

    const Value result = e_.ins().shuffle(source, dest, mask);

    e_.store_vr(vd, result);
}
