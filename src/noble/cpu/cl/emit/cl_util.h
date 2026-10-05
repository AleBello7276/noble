#pragma once

#include "Logger.h"
#include "cpu/cl/CraneliftJIT.h"
#include "cranelift.h"
#include <assert.h>

#include <cstdint>

using namespace cranelift;

using EmitterHandler = void (*)(EmitterContext& e_, InstructionInfo& info_);

#define CLHandler(name) inline void cl_##name##_handler(EmitterContext& e_, InstructionInfo& info_)

constexpr std::int32_t GPROffset(size_t index) {
    return static_cast<std::int32_t>(offsetof(PPCContext, GPRs) + index * sizeof(GPR));
}

constexpr std::int32_t FPROffset(size_t index) {
    return static_cast<std::int32_t>(offsetof(PPCContext, FPRs) + index * sizeof(FPR));
}

constexpr std::int32_t SPROffset(size_t struct_offset) {
    return static_cast<std::int32_t>(offsetof(PPCContext, SPRs) + struct_offset);
}

constexpr std::int32_t CRFieldBitOffset(size_t field_index, size_t bit_index = 0) {
    return static_cast<std::int32_t>(offsetof(PPCContext, ControlRegister) + (4 * field_index) + bit_index);
}

constexpr std::int32_t GetXER_CA_Offset() {
    return static_cast<std::int32_t>(SPROffset(offsetof(SPRState, XER)) + offsetof(XERr, CA));
}

constexpr std::int32_t GetXER_SO_Offset() {
    return static_cast<std::int32_t>(SPROffset(offsetof(SPRState, XER)) + offsetof(XERr, SO));
}

constexpr std::int32_t VROffset(size_t index) {
    return static_cast<std::int32_t>(offsetof(PPCContext, VRs) + index * sizeof(Vector128));
}

// TODO: template this
inline Value add_did_carry_imm32(EmitterContext& e_, Value lhs, uint32_t rhs) {
    return e_.ins().icmp_imm_u(IntCC::CL_INTCC_UNSIGNED_GREATER_THAN, e_.ins().ireduce(types::I32(), lhs),
                               ~rhs);
}

// TODO: template this
inline Value sub_did_carry_imm32(EmitterContext& e_, uint32_t lhs, Value rhs) {
    return e_.ins().icmp_imm_u(IntCC::CL_INTCC_UNSIGNED_LESS_THAN_OR_EQUAL,
                               e_.ins().ireduce(types::I32(), rhs), lhs);
}

inline Value sub_with_carry_did_carry32(EmitterContext& e_, Value lhs, Value rhs, Value ca) {
    const Value lhs32 = e_.ins().ireduce(types::I32(), lhs);
    const Value rhs32 = e_.ins().ireduce(types::I32(), rhs);
    const Value ca32 = e_.ins().uextend(types::I32(), ca);

    // {rb_plus_ca, overflow} = RB + CA
    //  CA_out = overflow || rb_plus_ca > RA
    const auto [adjusted_rhs, overflow] = e_.ins().uadd_overflow(rhs32, ca32);
    const Value greater = e_.ins().icmp(IntCC::CL_INTCC_UNSIGNED_GREATER_THAN, adjusted_rhs, lhs32);
    return e_.ins().bor(overflow, greater);
}

template <unsigned Bits>
constexpr std::int64_t sign_extend(std::uint32_t v) noexcept {
    static_assert(Bits > 0 && Bits < 32);

    constexpr auto sign = std::uint32_t{1} << (Bits - 1);
    constexpr auto mask = (std::uint32_t{1} << Bits) - 1;

    v &= mask;

    return (v & sign) ? static_cast<std::int64_t>(v) - (std::int64_t{1} << Bits) :
                        static_cast<std::int64_t>(v);
}

template <unsigned Bits>
constexpr std::uint64_t zero_extend(std::uint32_t v) noexcept {
    static_assert(Bits > 0 && Bits < 32);

    constexpr auto mask = (std::uint32_t{1} << Bits) - 1;
    return v & mask;
}

// returns the appropriate cranelift type from the bit count
template <unsigned Bits>
constexpr auto type_from_bits() {
    if constexpr (Bits == 8)
        return types::I8();
    else if constexpr (Bits == 16)
        return types::I16();
    else if constexpr (Bits == 32)
        return types::I32();
    else if constexpr (Bits == 64)
        return types::I64();
    else
        static_assert(Bits == 8 || Bits == 16 || Bits == 32 || Bits == 64);
}

// loads and converts a value from memory at the given EA
template <unsigned Bits>
inline Value emit_load_value(EmitterContext& e_, Value ea) {
    static_assert(Bits == 8 || Bits == 16 || Bits == 32 || Bits == 64);

    // read memory
    Value value = e_.load_memory(ea, e_.i64(0), type_from_bits<Bits>());

    // skip byte-swap if swapping 1 byte
    if constexpr (Bits > 8)
        value = e_.ins().bswap(value);

    // don't zero extend to 64 if already 64
    if constexpr (Bits < 64)
        value = e_.zext64(value);

    return value;
}

// emits displacement form loads
template <unsigned Bits, bool Update = false>
inline void emit_load(EmitterContext& e_, InstructionInfo& info_) {
    static_assert(Bits == 8 || Bits == 16 || Bits == 32 || Bits == 64);

    const auto ra = info_.mInst.field_ra();
    const auto rd = info_.mInst.field_rd();

    // load base
    Value base;
    if constexpr (Update) {
        // update-form loads require RA != 0
        base = e_.load_gpr(ra);
    } else {
        // normal form: RA == 0 means base is immediate 0
        base = ra ? e_.load_gpr(ra) : e_.i64(0);
    }

    // load displacement
    Value off;
    if constexpr (Bits == 64) {
        // the decoder returns the signed byte displacement with its implicit low bits included
        off = e_.i64(info_.mInst.field_ds());
    } else {
        const auto d = info_.mInst.field_offset();
        off = e_.iconst(types::I64(), sign_extend<16>(d));
    }

    // calculate effective address
    Value ea = e_.ins().iadd(base, off);

    // read memory
    Value value = emit_load_value<Bits>(e_, ea);

    // store read value to destination
    e_.store_gpr(rd, value);

    // update form instructions store EA back to ra
    if constexpr (Update)
        e_.store_gpr(ra, ea);
}

// emits indexed form loads
template <unsigned Bits, bool Update = false>
inline void emit_load_indexed(EmitterContext& e_, InstructionInfo& info_) {
    static_assert(Bits == 8 || Bits == 16 || Bits == 32 || Bits == 64);

    const auto ra = info_.mInst.field_ra();
    const auto rb = info_.mInst.field_rb();
    const auto rd = info_.mInst.field_rd();

    // load base
    Value base;
    if constexpr (Update) {
        // update-form loads require RA != 0
        base = e_.load_gpr(ra);
    } else {
        // normal form: RA == 0 means base is immediate 0
        base = ra ? e_.load_gpr(ra) : e_.i64(0);
    }

    // load index
    Value index = e_.load_gpr(rb);

    // calculate effective address
    Value ea = e_.ins().iadd(base, index);

    // read memory
    Value value = emit_load_value<Bits>(e_, ea);

    // store read value to destination
    e_.store_gpr(rd, value);

    // update form instructions store EA back to ra
    if constexpr (Update)
        e_.store_gpr(ra, ea);
}

// prepares a GPR value for storing to memory
template <unsigned Bits>
inline Value emit_store_value(EmitterContext& e_, Value value) {
    static_assert(Bits == 8 || Bits == 16 || Bits == 32 || Bits == 64);

    // truncate value to the store size
    if constexpr (Bits < 64)
        value = e_.ins().ireduce(type_from_bits<Bits>(), value);

    // skip byte-swap if swapping 1 byte
    if constexpr (Bits > 8)
        value = e_.ins().bswap(value);

    return value;
}

// emits displacement form stores
template <unsigned Bits, bool Update = false>
inline void emit_store(EmitterContext& e_, InstructionInfo& info_) {
    static_assert(Bits == 8 || Bits == 16 || Bits == 32 || Bits == 64);

    const auto ra = info_.mInst.field_ra();
    const auto rs = info_.mInst.field_rs();

    // load base
    Value base;
    if constexpr (Update) {
        // update-form stores require RA != 0
        base = e_.load_gpr(ra);
    } else {
        // normal form: RA == 0 means base is immediate 0
        base = ra ? e_.load_gpr(ra) : e_.i64(0);
    }

    // load displacement
    Value off;
    if constexpr (Bits == 64) {
        // the decoder returns the signed byte displacement with its implicit low bits included
        off = e_.i64(info_.mInst.field_ds());
    } else {
        const auto d = info_.mInst.field_offset();
        off = e_.iconst(types::I64(), sign_extend<16>(d));
    }

    // calculate effective address
    Value ea = e_.ins().iadd(base, off);

    // load and prepare source value
    Value value = emit_store_value<Bits>(e_, e_.load_gpr(rs));

    // write value to memory
    e_.store_memory(ea, value);

    // update form instructions store EA back to ra
    if constexpr (Update)
        e_.store_gpr(ra, ea);
}

// emits indexed form stores
template <unsigned Bits, bool Update = false>
inline void emit_store_indexed(EmitterContext& e_, InstructionInfo& info_) {
    static_assert(Bits == 8 || Bits == 16 || Bits == 32 || Bits == 64);

    const auto ra = info_.mInst.field_ra();
    const auto rb = info_.mInst.field_rb();
    const auto rs = info_.mInst.field_rs();

    // load base
    Value base;
    if constexpr (Update) {
        // update-form stores require RA != 0
        base = e_.load_gpr(ra);
    } else {
        // normal form: RA == 0 means base is immediate 0
        base = ra ? e_.load_gpr(ra) : e_.i64(0);
    }

    // load index
    Value index = e_.load_gpr(rb);

    // calculate effective address
    Value ea = e_.ins().iadd(base, index);

    // load and prepare source value
    Value value = emit_store_value<Bits>(e_, e_.load_gpr(rs));

    // write value to memory
    e_.store_memory(ea, value);

    // update form instructions store EA back to ra
    if constexpr (Update)
        e_.store_gpr(ra, ea);
}

/*
    Floating point
*/

template <unsigned Bits>
inline Value emit_fload_value(EmitterContext& e_, Value ea) {
    static_assert(Bits == 32 || Bits == 64);

    if constexpr (Bits == 32) {
        // Load guest big-endian float as raw integer bits.
        Value bits = e_.load_memory(ea, e_.i64(0), types::I32());
        bits = e_.ins().bswap(bits);

        // I32 bits -> F32.
        const Value f32 = e_.ins().bitcast(types::F32(), MemFlags{}, bits);

        // PPC FPR receives the single-precision value represented
        // in the register's double-precision format.
        return e_.ins().fpromote(types::F64(), f32);
    } else {
        Value bits = e_.load_memory(ea, e_.i64(0), types::I64());
        bits = e_.ins().bswap(bits);

        // I64 bits -> F64.
        return e_.ins().bitcast(types::F64(), MemFlags{}, bits);
    }
}

template <unsigned Bits, bool Update = false>
inline void emit_fload(EmitterContext& e_, InstructionInfo& info_) {
    static_assert(Bits == 32 || Bits == 64);

    const auto ra = info_.mInst.field_ra();
    const auto frd = info_.mInst.field_frd();

    Value base;

    if constexpr (Update) {
        // Update forms require RA != 0.
        base = e_.load_gpr(ra);
    } else {
        base = ra ? e_.load_gpr(ra) : e_.i64(0);
    }

    const auto d = info_.mInst.field_offset();
    const Value ea = e_.ins().iadd_imm_s(base, sign_extend<16>(d));
    const Value value = emit_fload_value<Bits>(e_, ea);
    e_.store_fpr(frd, value);

    if constexpr (Update)
        e_.store_gpr(ra, ea);
}
template <unsigned Bits, bool Update = false>
inline void emit_fload_indexed(EmitterContext& e_, InstructionInfo& info_) {
    static_assert(Bits == 32 || Bits == 64);

    const auto ra = info_.mInst.field_ra();
    const auto rb = info_.mInst.field_rb();
    const auto frd = info_.mInst.field_frd();

    Value base;

    if constexpr (Update) {
        base = e_.load_gpr(ra);
    } else {
        base = ra ? e_.load_gpr(ra) : e_.i64(0);
    }

    const Value index = e_.load_gpr(rb);
    const Value ea = e_.ins().iadd(base, index);
    const Value value = emit_fload_value<Bits>(e_, ea);
    e_.store_fpr(frd, value);

    if constexpr (Update)
        e_.store_gpr(ra, ea);
}

template <unsigned Bits>
inline Value emit_fstore_value(EmitterContext& e_, Value value) {
    static_assert(Bits == 32 || Bits == 64);

    if constexpr (Bits == 32) {
        // F64 -> rounded F32.
        const Value f32 = e_.ins().fdemote(types::F32(), value);

        // F32 -> raw I32 bits.
        Value bits = e_.ins().bitcast(types::I32(), MemFlags{}, f32);

        // Guest memory is big-endian.
        bits = e_.ins().bswap(bits);
        return bits;
    } else {
        // F64 -> raw I64 bits.
        Value bits = e_.ins().bitcast(types::I64(), MemFlags{}, value);
        bits = e_.ins().bswap(bits);

        return bits;
    }
}

template <unsigned Bits, bool Update = false>
inline void emit_fstore(EmitterContext& e_, InstructionInfo& info_) {
    static_assert(Bits == 32 || Bits == 64);

    const auto ra = info_.mInst.field_ra();
    const auto frs = info_.mInst.field_frs();

    Value base;

    if constexpr (Update) {
        base = e_.load_gpr(ra);
    } else {
        base = ra ? e_.load_gpr(ra) : e_.i64(0);
    }

    const auto d = info_.mInst.field_offset();
    const Value ea = e_.ins().iadd_imm_s(base, sign_extend<16>(d));
    const Value value = emit_fstore_value<Bits>(e_, e_.load_fpr(frs));
    e_.store_memory(ea, value);

    if constexpr (Update)
        e_.store_gpr(ra, ea);
}

template <unsigned Bits, bool Update = false>
inline void emit_fstore_indexed(EmitterContext& e_, InstructionInfo& info_) {
    static_assert(Bits == 32 || Bits == 64);

    const auto ra = info_.mInst.field_ra();
    const auto rb = info_.mInst.field_rb();
    const auto frs = info_.mInst.field_frs();

    Value base;

    if constexpr (Update) {
        base = e_.load_gpr(ra);
    } else {
        base = ra ? e_.load_gpr(ra) : e_.i64(0);
    }

    const Value index = e_.load_gpr(rb);
    const Value ea = e_.ins().iadd(base, index);
    const Value value = emit_fstore_value<Bits>(e_, e_.load_fpr(frs));
    e_.store_memory(ea, value);

    if constexpr (Update)
        e_.store_gpr(ra, ea);
}

// from xenon
constexpr uint64_t PPCMASK(uint64_t mb, uint64_t me) {
    const uint64_t mask = ~0ULL << (~(me - mb) & 63);
    return (mask >> (mb & 63)) | (mask << ((64 - mb) & 63));
}

// from xenia
static inline bool InstrCheck_rlx_only_needs_low(unsigned rotation, uint64_t mask) {
    uint32_t mask32 = static_cast<uint32_t>(mask);
    if (static_cast<uint64_t>(mask32) != mask) {
        return false;
    }
    uint32_t all_ones_32 = ~0U;
    all_ones_32 <<= rotation;

    return all_ones_32 == mask32;  // mask is only 32 bits and all bits from the
                                   // rotation are discarded
}

inline Value fctid_convert(EmitterContext& e_, Value src, Value rounded) {
    // PPC requires NaN -> 0x8000000000000000
    const Value is_nan = e_.ins().fcmp(FloatCC::CL_FLOATCC_UNORDERED, src, src);

    // cranelift saturation matches PPC numeric overflow behavior:
    //
    // > INT64_MAX -> INT64_MAX
    // < INT64_MIN -> INT64_MIN
    //
    // NaN -> 0 which fix below
    const Value converted = e_.ins().fcvt_to_sint_sat(types::I64(), rounded);
    const Value integer = e_.ins().select(is_nan, e_.i64(INT64_MIN), converted);

    // FCTID stores the integer BIT PATTERN in the FPR
    return e_.ins().bitcast(types::F64(), MemFlags{}, integer);
}

constexpr std::int32_t ReserveAddressOffset() {
    return static_cast<std::int32_t>(offsetof(PPCContext, reserve_address));
}

constexpr std::int32_t ReserveValueOffset() {
    return static_cast<std::int32_t>(offsetof(PPCContext, reserve_value));
}

constexpr std::int32_t ReserveValidOffset() {
    return static_cast<std::int32_t>(offsetof(PPCContext, reserve_valid));
}

/*
    Vxu stuff
*/

inline std::array<uint8_t, 16> make_word_shuffle_mask(uint32_t x, uint32_t y, uint32_t z, uint32_t w) {
    return {
        uint8_t(x * 4 + 0), uint8_t(x * 4 + 1), uint8_t(x * 4 + 2), uint8_t(x * 4 + 3),
        uint8_t(y * 4 + 0), uint8_t(y * 4 + 1), uint8_t(y * 4 + 2), uint8_t(y * 4 + 3),
        uint8_t(z * 4 + 0), uint8_t(z * 4 + 1), uint8_t(z * 4 + 2), uint8_t(z * 4 + 3),
        uint8_t(w * 4 + 0), uint8_t(w * 4 + 1), uint8_t(w * 4 + 2), uint8_t(w * 4 + 3),
    };
}

inline Value make_i32x4(EmitterContext& e_, Value x, Value y, Value z, Value w) {
    Value v = e_.ins().splat(types::I32X4(), x);
    v = e_.ins().insertlane(v, y, 1);
    v = e_.ins().insertlane(v, z, 2);
    v = e_.ins().insertlane(v, w, 3);
    return v;
}

inline Value make_f32x4(EmitterContext& e_, Value x, Value y, Value z, Value w) {
    Value v = e_.ins().splat(types::F32X4(), x);
    v = e_.ins().insertlane(v, y, 1);
    v = e_.ins().insertlane(v, z, 2);
    v = e_.ins().insertlane(v, w, 3);
    return v;
}
