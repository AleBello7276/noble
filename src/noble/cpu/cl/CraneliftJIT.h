#pragma once

#include "cpu/JITBackend.h"
#include "cranelift.h"
#include "emulator/Memory.h"

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

    mutable std::mutex mutex_;
    // std::unordered_map<GuestAddress, codec::PpcIns> decoded_;
    std::unordered_map<GuestAddress, PPCBasicBlock> basicBlocks_;
    std::unordered_map<GuestAddress, std::shared_ptr<const JITBlock>> blocks_;
};
