#pragma once

#include "cpu/JITBackend.h"
#include "cranelift.h"
#include "emulator/Memory.h"

#include <bit>
#include <cstdint>

// set to 0 for a direct load or store on every register access
#ifndef NOBLE_CRANELIFT_REGISTER_CACHE
#define NOBLE_CRANELIFT_REGISTER_CACHE 0
#endif

class CraneliftJIT;

/* all the stuff needed to emit an instruction */
struct EmitterContext {
public:
    // bound native backedges so the dispatcher can observe stop and scheduler requests
    cranelift::Variable branchBudget = cranelift::INVALID_ID;
    EmitterContext(PPCFuncMap ranges, cranelift::Value state, cranelift::Value base,
                   cranelift::JITModule& jit_, cranelift::FunctionBuilder& builder_,
                   Memory* memory_ = nullptr, CraneliftJIT* backend_ = nullptr)
        : mFuncRanges(ranges), vCpuState(state), vMemBase(base), jit(jit_), builder(builder_),
          memory(memory_), backend(backend_) {}

    bool isBlockInMap(GuestAddress address) { return clBlockMap.contains(address); }

    /* return an i64 Value and load it from the context on first use in this block */
    cranelift::Value load_gpr(size_t index);

    /* update a cached gpr with an i64 value */
    cranelift::Value store_gpr(size_t index, cranelift::Value value);

    /* return an f64 value and load it from the context on first use in this block */
    cranelift::Value load_fpr(size_t index);

    /* update a cached fpr with an f64 value */
    cranelift::Value store_fpr(size_t index, cranelift::Value value);

    /* read an i64 spr at its register offset within SPRState */
    cranelift::Value load_spr(size_t struct_offset);
    cranelift::Value store_spr(size_t struct_offset, cranelift::Value value);

    /* read a named spr as an i64 value from PPCContext */
    cranelift::Value load_spr(eSPR type);
    cranelift::Value store_spr(eSPR type, cranelift::Value value);

    void store_ca(cranelift::Value value);
    cranelift::Value load_ca();

    cranelift::Value load_msr();
    void store_msr(cranelift::Value value);

    cranelift::Value load_vr(uint32_t index, cranelift::Type type);
    void store_vr(size_t index, cranelift::Value value);

    cranelift::Value byteswap_v128(cranelift::Value value) {
        static constexpr std::array<std::uint8_t, 16> mask
            = {15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0};

        return ins().shuffle(value, value, mask);
    }

public:
    /* flush cached register states to context */
    void FlushState();

    /* clear cached values after a flush */
    void InvalidateState();

    /*  these are helpers that wraps cranelift instructions and flush / invalidate
        the cache when needed */
    void SwitchToBlock(cranelift::Block block);
    cranelift::Inst Return();
    cranelift::Inst Jump(cranelift::Block destination, std::span<const cranelift::Value> args = {});
    cranelift::Inst Branch(cranelift::Value condition, cranelift::Block then_block,
                           std::span<const cranelift::Value> then_args, cranelift::Block else_block,
                           std::span<const cranelift::Value> else_args);
    cranelift::Inst Call(cranelift::FuncRef function, std::span<const cranelift::Value> args = {});

    /* call a guest function with state and memory base parameters */
    cranelift::Inst CallGuest(cranelift::FuncRef function);

    cranelift::Inst CallIndirect(cranelift::SigRef signature, cranelift::Value callee,
                                 std::span<const cranelift::Value> args = {});

public:
    // const helpers
    cranelift::Value iconst_zero(cranelift::Type type) { return ins().iconst(type, 0); }
    cranelift::Value iconst(cranelift::Type type, size_t immediate) { return ins().iconst(type, immediate); }
    cranelift::Value i8(size_t immediate) { return iconst(cranelift::types::I8(), immediate); }
    cranelift::Value i16(size_t immediate) { return iconst(cranelift::types::I16(), immediate); }
    cranelift::Value i32(size_t immediate) { return iconst(cranelift::types::I32(), immediate); }
    cranelift::Value i64(size_t immediate) { return iconst(cranelift::types::I64(), immediate); }

    cranelift::Value f32(float immediate) { return ins().f32const(std::bit_cast<uint32_t>(immediate)); }
    cranelift::Value f64(double immediate) { return ins().f64const(std::bit_cast<uint64_t>(immediate)); }

    // extend helpers
    cranelift::Value zext16(cranelift::Value value) { return ins().uextend(cranelift::types::I16(), value); }
    cranelift::Value zext32(cranelift::Value value) { return ins().uextend(cranelift::types::I32(), value); }
    cranelift::Value zext64(cranelift::Value value) { return ins().uextend(cranelift::types::I64(), value); }
    cranelift::Value sext16(cranelift::Value value) { return ins().sextend(cranelift::types::I16(), value); }
    cranelift::Value sext32(cranelift::Value value) { return ins().sextend(cranelift::types::I32(), value); }
    cranelift::Value sext64(cranelift::Value value) { return ins().sextend(cranelift::types::I64(), value); }

    // truncate helpers
    cranelift::Value reduce8(cranelift::Value value) { return ins().ireduce(cranelift::types::I8(), value); }
    cranelift::Value reduce16(cranelift::Value value) {
        return ins().ireduce(cranelift::types::I16(), value);
    }
    cranelift::Value reduce32(cranelift::Value value) {
        return ins().ireduce(cranelift::types::I32(), value);
    }

    // load memory - ea
    cranelift::Value load_memory(cranelift::Value ea, cranelift::Type load_type);
    // load memory - base + offset
    cranelift::Value load_memory(cranelift::Value base, cranelift::Value offset, cranelift::Type load_type);

    // store memory - ea
    void store_memory(cranelift::Value ea, cranelift::Value value);
    // store memory - base + offset
    void store_memory(cranelift::Value base, cranelift::Value offset, cranelift::Value value);

    cranelift::InstBuilder ins() { return builder.ins(); }

    void comment(cranelift::Inst instruction, std::string text) {
        builder.comment(instruction, std::move(text));
    }
    bool comment(std::string text) { return builder.comment(std::move(text)); }

    template <bool Signed = true>
    void record_cr(size_t field, cranelift::Value lhs, cranelift::Value rhs);

    template <bool Signed = true>
    void record_cr(size_t field, cranelift::Value lhs);

    cranelift::Value get_cr_field(size_t field, size_t bit);

    void update_fpscr(bool rc);

    void store_fpscr(cranelift::Value value);
    cranelift::Value load_fpscr();
    void copy_fpscr_to_cr1();

    cranelift::Value load_clock();

    cranelift::Value to_single(cranelift::Value value) {
        return ins().fpromote(cranelift::types::F64(), ins().fdemote(cranelift::types::F32(), value));
    }

public:
    PPCFuncMap mFuncRanges;
    std::unordered_map<GuestAddress, cranelift::Block> clBlockMap;

    cranelift::Block BlockLookup(GuestAddress address) {
        if (address < mFuncRanges.mStart || address >= mFuncRanges.mEnd)
            return cranelift::INVALID_ID;

        if (clBlockMap.contains(address))
            return clBlockMap.at(address);

        cranelift::Block new_block = builder.create_block();
        clBlockMap.try_emplace(address, new_block);

        return new_block;
    }

    cranelift::Value vCpuState;
    cranelift::Value vMemBase;
    cranelift::JITModule& jit;
    cranelift::FunctionBuilder& builder;
    cranelift::Value returnAddress = cranelift::INVALID_ID;
    bool terminated = false;

    Memory* memory;
    CraneliftJIT* backend;

private:
    // add the zero extended guest address to the host mapping base
    cranelift::Value memory_address(cranelift::Value ea);

    struct CachedValue {
        cranelift::Value value = cranelift::INVALID_ID;
        bool dirty = false;
    };

#if NOBLE_CRANELIFT_REGISTER_CACHE
    std::array<CachedValue, PPCContext::GPR_COUNT> gprCache_{};
    std::array<CachedValue, PPCContext::FPR_COUNT> fprCache_{};
    std::map<size_t, CachedValue> sprCache_{};
#endif
};
