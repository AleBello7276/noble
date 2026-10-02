#pragma once

#include "Logger.h"
#include "cpu/cl/CraneliftJIT.h"
#include "cranelift.h"
#include <assert.h>

using namespace cranelift;

using EmitterHandler = void (*)(EmitterContext& e_, InstructionInfo& info_);

#define CLHandler(name) inline void cl_##name##_handler(EmitterContext& e_, InstructionInfo& info_)

#include <cstdint>

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
        value = e_.zext(types::I64(), value);

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
        const auto ds = info_.mInst.field_ds();
        off = e_.iconst(types::I64(), sign_extend<16>(ds << 2));
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
        const auto ds = info_.mInst.field_ds();
        off = e_.iconst(types::I64(), sign_extend<16>(ds << 2));
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
