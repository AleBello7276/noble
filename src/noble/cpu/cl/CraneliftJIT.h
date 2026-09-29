#pragma once

#include "cpu/JITBackend.h"
#include "cranelift.h"
#include "emulator/Memory.h"
#include <array>
#include <map>
#include <span>

// set to 0 for a direct load or store on every register access
#ifndef NOBLE_CRANELIFT_REGISTER_CACHE
#define NOBLE_CRANELIFT_REGISTER_CACHE 1
#endif

struct InstructionInfo {
    codec::Ins mInst;
    GuestAddress mAddress;
};

/* all the stuff needed to emit an instruction */
struct EmitterContext {
public:
    EmitterContext(PPCFuncMap ranges, cranelift::Value state, cranelift::Value base,
                   cranelift::JITModule& jit_, cranelift::FunctionBuilder& builder_)
        : mFuncRanges(ranges), vCpuState(state), vMemBase(base), jit(jit_), builder(builder_) {}

    bool isBlockInMap(GuestAddress address) { return clBlockMap.contains(address); }

    /* return an i64 Value and load it from the context on first use in this block */
    cranelift::Value LoadGPR(size_t index);

    /* update a cached gpr with an i64 value */
    cranelift::Value StoreGPR(size_t index, cranelift::Value value);

    /* return an f64 value and load it from the context on first use in this block */
    cranelift::Value LoadFPR(size_t index);

    /* update a cached fpr with an f64 value */
    cranelift::Value StoreFPR(size_t index, cranelift::Value value);

    /* read an i64 spr at its register offset within SPRState */
    cranelift::Value LoadSPR(size_t struct_offset);
    cranelift::Value StoreSPR(size_t struct_offset, cranelift::Value value);

    /* read a named spr as an i64 value from PPCContext */
    cranelift::Value LoadSPR(eSPR type);
    cranelift::Value StoreSPR(eSPR type, cranelift::Value value);

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

    /* basically tail calls a guest function */
    cranelift::Inst CallGuest(cranelift::FuncRef function);

    cranelift::Inst CallIndirect(cranelift::SigRef signature, cranelift::Value callee,
                                 std::span<const cranelift::Value> args = {});

public:
    PPCFuncMap mFuncRanges;
    std::unordered_map<GuestAddress, cranelift::Block> clBlockMap;

    cranelift::Value vCpuState;
    cranelift::Value vMemBase;
    cranelift::JITModule& jit;
    cranelift::FunctionBuilder& builder;

private:
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

class CraneliftJIT final : public JITBackend {
public:
    explicit CraneliftJIT(Memory& memory);

    void CompilePPCModule(PPCModule& module) override;

    void CompileJITBlock(GuestAddress address) override;

    void InvalidateBlock(GuestAddress address) override;

    void InvalidateRegion(GuestAddress from, GuestAddress to) override;

    JITBlock FindBlock(GuestAddress address) const;

private:
    Memory& memory_;
    cranelift::JITModule jit_module_;
    cranelift::JITBuilder jit_builder_;

    mutable std::mutex mutex_;
    // std::unordered_map<GuestAddress, codec::PpcIns> decoded_;
    std::unordered_map<GuestAddress, PPCBasicBlock> basicBlocks_;
    std::unordered_map<GuestAddress, std::shared_ptr<const JITBlock>> blocks_;
};
