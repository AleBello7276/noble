#pragma once

#include "cpu/JITBackend.h"
#include "cranelift.h"
#include "emulator/Memory.h"

struct InstructionInfo {
    codec::Ins mInst;
    GuestAddress mAddress;
};

struct IRFunc {
    PPCFuncMap mRanges;
    cranelift::Value vCpuState;
    cranelift::Value vMemBase;

    IRFunc(PPCFuncMap ranges, cranelift::Value state, cranelift::Value base, cranelift::JITModule& jit_,
           cranelift::FunctionBuilder& builder_)
        : mRanges(ranges), vCpuState(state), vMemBase(base), jit(jit_), builder(builder_) {}

    std::unordered_map<GuestAddress, cranelift::Block> clBlockMap;

    bool isBlockInMap(GuestAddress address) { return clBlockMap.contains(address); }

    cranelift::JITModule& jit;
    cranelift::FunctionBuilder& builder;
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
