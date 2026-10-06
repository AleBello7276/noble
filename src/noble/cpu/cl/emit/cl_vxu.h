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
    // interpret shuffled bytes as host vector lanes with the least significant byte first
    const MemFlags lane_flags
        = e_.builder.memflags_with_endianness(e_.builder.memflags_new(), Endianness::CL_ENDIANNESS_LITTLE);

    switch (type) {
    case 0: {
        static constexpr std::array<uint8_t, 16> mask = {
            14, 16, 16, 16, 13, 16, 16, 16, 12, 16, 16, 16, 15, 16, 16, 16,
        };

        Value value = e_.ins().shuffle(source, zero, mask);
        value = e_.ins().bitcast(types::I32X4(), lane_flags, value);

        return e_.ins().bor(value, e_.ins().splat(types::I32X4(), e_.i32(0x3F800000u)));
    }

    case 1: {
        static constexpr std::array<uint8_t, 16> mask = {
            14, 15, 16, 16, 12, 13, 16, 16, 16, 16, 16, 16, 16, 16, 16, 16,
        };

        Value value = e_.ins().shuffle(source, zero, mask);
        value = e_.ins().bitcast(types::I32X4(), lane_flags, value);
        value = e_.ins().sshr_imm_u(e_.ins().ishl_imm_u(value, 16), 16);

        const Value base
            = make_i32x4(e_, e_.i32(0x40400000u), e_.i32(0x40400000u), e_.i32(0), e_.i32(0x3F800000u));

        value = e_.ins().iadd(value, base);

        return unpack_overflow_nan(e_, value, 0x403F8000u);
    }

    case 2: {
        const Value words = e_.ins().bitcast(types::I32X4(), lane_flags, source);
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
        const Value halves = e_.ins().bitcast(types::I16X8(), lane_flags, shuffled);

        const Value x = half_to_f32(e_, e_.ins().extractlane(halves, 0));
        const Value y = half_to_f32(e_, e_.ins().extractlane(halves, 1));

        return make_f32x4(e_, x, y, e_.f32(0.0f), e_.f32(1.0f));
    }

    case 4: {
        static constexpr std::array<uint8_t, 16> mask = {
            10, 11, 16, 16, 8, 9, 16, 16, 14, 15, 16, 16, 12, 13, 16, 16,
        };

        Value value = e_.ins().shuffle(source, zero, mask);
        value = e_.ins().bitcast(types::I32X4(), lane_flags, value);
        value = e_.ins().sshr_imm_u(e_.ins().ishl_imm_u(value, 16), 16);
        value = e_.ins().iadd(value, e_.ins().splat(types::I32X4(), e_.i32(0x40400000u)));

        return unpack_overflow_nan(e_, value, 0x403F8000u);
    }

    case 5: {
        static constexpr std::array<uint8_t, 16> mask = {
            10, 11, 8, 9, 14, 15, 12, 13, 16, 16, 16, 16, 16, 16, 16, 16,
        };

        const Value shuffled = e_.ins().shuffle(source, zero, mask);
        const Value halves = e_.ins().bitcast(types::I16X8(), lane_flags, shuffled);

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
        value = e_.ins().bitcast(types::I32X4(), lane_flags, value);

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

inline void emit_lvsl(EmitterContext& e_, uint32_t vd, uint32_t ra, uint32_t rb) {
    const Value base = ra ? e_.load_gpr(ra) : e_.i64(0);
    const Value ea = e_.ins().iadd(base, e_.load_gpr(rb));

    const Value sh = e_.ins().ireduce(types::I8(), e_.ins().band_imm_u(ea, 0xF));

    static constexpr std::array<uint8_t, 16> bytes = {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};

    const Value indices = e_.ins().vconst(types::I8X16(), bytes);
    const Value result = e_.ins().iadd(indices, e_.ins().splat(types::I8X16(), sh));

    e_.store_vr(vd, result);
}

CLHandler(lvsl) {
    emit_lvsl(e_, info_.mInst.field_vd(), info_.mInst.field_ra(), info_.mInst.field_rb());
}

CLHandler(lvsl128) {
    emit_lvsl(e_, info_.mInst.field_vds128(), info_.mInst.field_ra(), info_.mInst.field_rb());
}

inline void emit_vperm(EmitterContext& e_, uint32_t vd, uint32_t va, uint32_t vb, uint32_t vc) {
    // vector loads reverse all bytes, so translate guest indices across the full vector
    const Value swap = e_.ins().splat(types::I8X16(), e_.i8(0x0F));
    const Value mask = e_.ins().splat(types::I8X16(), e_.i8(0x1F));
    const Value source_bit = e_.ins().splat(types::I8X16(), e_.i8(0x10));

    Value control = e_.ins().bxor(e_.load_vr(vc, types::I8X16()), swap);
    control = e_.ins().band(control, mask);

    const Value a = e_.ins().swizzle(e_.load_vr(va, types::I8X16()), control);
    const Value b = e_.ins().swizzle(e_.load_vr(vb, types::I8X16()), e_.ins().bxor(control, source_bit));

    e_.store_vr(vd, e_.ins().bor(a, b));
}

CLHandler(vperm) {
    emit_vperm(e_, info_.mInst.field_vd(), info_.mInst.field_va(), info_.mInst.field_vb(),
               info_.mInst.field_vc());
}

CLHandler(vperm128) {
    emit_vperm(e_, info_.mInst.field_vds128(), info_.mInst.field_va128(), info_.mInst.field_vb128(),
               info_.mInst.field_vc());
}

inline void emit_stvlx(EmitterContext& e_, uint32_t vs, uint32_t ra, uint32_t rb) {
    const Value base = ra ? e_.load_gpr(ra) : e_.i64(0);
    const Value ea = e_.ins().iadd(base, e_.load_gpr(rb));

    const Value aligned = e_.ins().band_imm_u(ea, ~0xFULL);
    const Value eb = e_.ins().ireduce(types::I8(), e_.ins().band_imm_u(ea, 0xF));

    const Value old = e_.load_memory(aligned, types::I8X16());
    const Value source = e_.byteswap_v128(e_.load_vr(vs, types::I8X16()));

    static constexpr std::array<uint8_t, 16> indices_bytes
        = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

    const Value indices = e_.ins().vconst(types::I8X16(), indices_bytes);
    const Value offset = e_.ins().splat(types::I8X16(), eb);

    const Value source_indices = e_.ins().isub(indices, offset);
    const Value shifted = e_.ins().swizzle(source, source_indices);

    const Value replace = e_.ins().icmp(IntCC::CL_INTCC_UNSIGNED_GREATER_THAN_OR_EQUAL, indices, offset);

    const Value result = e_.ins().bitselect(replace, shifted, old);

    e_.store_memory(aligned, result);
}

CLHandler(stvlx) {
    emit_stvlx(e_, info_.mInst.field_vs(), info_.mInst.field_ra(), info_.mInst.field_rb());
}

CLHandler(stvlxl) {
    emit_stvlx(e_, info_.mInst.field_vs(), info_.mInst.field_ra(), info_.mInst.field_rb());
}

CLHandler(stvlx128) {
    emit_stvlx(e_, info_.mInst.field_vds128(), info_.mInst.field_ra(), info_.mInst.field_rb());
}

CLHandler(stvlxl128) {
    emit_stvlx(e_, info_.mInst.field_vds128(), info_.mInst.field_ra(), info_.mInst.field_rb());
}

inline void emit_stvrx(EmitterContext& e_, uint32_t vs, uint32_t ra, uint32_t rb) {
    const Value base = ra ? e_.load_gpr(ra) : e_.i64(0);
    const Value ea = e_.ins().iadd(base, e_.load_gpr(rb));
    const Value eb = e_.ins().ireduce(types::I8(), e_.ins().band_imm_u(ea, 0xF));

    const Block store = e_.builder.create_block();
    const Block done = e_.builder.create_block();

    const Value has_data = e_.ins().icmp_imm_u(IntCC::CL_INTCC_NOT_EQUAL, eb, 0);
    e_.Branch(has_data, store, {}, done, {});

    e_.SwitchToBlock(store);

    const Value aligned = e_.ins().band_imm_u(ea, ~0xFULL);
    const Value old = e_.load_memory(aligned, types::I8X16());
    const Value source = e_.byteswap_v128(e_.load_vr(vs, types::I8X16()));

    static constexpr std::array<uint8_t, 16> bytes = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

    const Value indices = e_.ins().vconst(types::I8X16(), bytes);
    const Value offset = e_.ins().splat(types::I8X16(), e_.ins().isub(e_.i8(16), eb));

    const Value shifted = e_.ins().swizzle(source, e_.ins().iadd(indices, offset));
    const Value replace
        = e_.ins().icmp(IntCC::CL_INTCC_UNSIGNED_LESS_THAN, indices, e_.ins().splat(types::I8X16(), eb));

    e_.store_memory(aligned, e_.ins().bitselect(replace, shifted, old));

    e_.Jump(done);
    e_.SwitchToBlock(done);
}

CLHandler(stvrx) {
    emit_stvrx(e_, info_.mInst.field_vs(), info_.mInst.field_ra(), info_.mInst.field_rb());
}

CLHandler(stvrxl) {
    emit_stvrx(e_, info_.mInst.field_vs(), info_.mInst.field_ra(), info_.mInst.field_rb());
}

CLHandler(stvrx128) {
    emit_stvrx(e_, info_.mInst.field_vds128(), info_.mInst.field_ra(), info_.mInst.field_rb());
}

CLHandler(stvrxl128) {
    emit_stvrx(e_, info_.mInst.field_vds128(), info_.mInst.field_ra(), info_.mInst.field_rb());
}
