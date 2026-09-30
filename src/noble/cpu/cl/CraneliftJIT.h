#pragma once

#include "EmitterContext.h"
#include "cpu/JITBackend.h"
#include "cranelift.h"
#include "emulator/Memory.h"
#include <array>
#include <map>
#include <optional>
#include <span>

#ifndef NOBLE_CRANELIFT_DEBUG
#define NOBLE_CRANELIFT_DEBUG 1
#endif

struct InstructionInfo {
    codec::Ins mInst;
    GuestAddress mAddress;
};

class CraneliftJIT final : public JITBackend {
public:
    explicit CraneliftJIT(Memory& memory);

    void CompilePPCModule(PPCModule& module) override;

    void CompileJITBlock(GuestAddress address) override;

    void InvalidateBlock(GuestAddress address) override;

    void InvalidateRegion(GuestAddress from, GuestAddress to) override;

    JITBlock FindBlock(GuestAddress address) const override;

    void SetHLERegistry(hle::Registry* registry) override;

    cranelift::FuncId FindFunctionId(GuestAddress address) const;

private:
    Memory& memory_;
    cranelift::JITModule jit_module_;
    cranelift::JITBuilder jit_builder_;
    hle::Registry* imports_ = nullptr;
    std::unordered_map<GuestAddress, XLoader::Import> importsByAddress_;

    void CompileImport(const XLoader::Import& import);

    mutable std::mutex mutex_;

    std::unordered_map<GuestAddress, PPCBasicBlock> basicBlocks_;
    std::unordered_map<GuestAddress, cranelift::FuncId> functionIds_;
    std::unordered_map<GuestAddress, std::shared_ptr<const JITBlock>> compiledBlocks_;
};
