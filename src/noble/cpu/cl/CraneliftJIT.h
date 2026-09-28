#pragma once

#include "cpu/JITBackend.h"
#include "cranelift.h"

class CraneliftJIT final : public JITBackend {
public:
    void CompilePPCModule(PPCModule& module) override;

    void CompileJITBlock(GuestAddress address) override;

    void InvalidateBlock(GuestAddress address) override;

    void InvalidateRegion(GuestAddress from, GuestAddress to) override;

private:
    std::mutex mutex_;
    std::map<GuestAddress, std::shared_ptr<const JITBlock>> blocks_;
};
