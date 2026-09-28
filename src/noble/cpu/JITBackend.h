#pragma once

#include "PpcContext.h"
#include <map>
#include <memory>
#include <mutex>

#include "emulator/PPCModule.h"

using JITBlock = void (*)(void* MemBase, PPCContext* Context);
using GuestAddress = uint32_t;

class JITBackend {
public:
    virtual ~JITBackend() = default;

    /* jit all blocks in a PPCModule which does function bound analysis and simple cfg at load */
    virtual void CompilePPCModule(PPCModule& module) = 0;

    /* JIT a block of instructions at address in guest memory */
    virtual void CompileJITBlock(GuestAddress address) = 0;

    /* invalidate and remove block at address */
    virtual void InvalidateBlock(GuestAddress address) = 0;

    /* invalidate and remove all blocks in the range, used for like unloading a module or whatever */
    virtual void InvalidateRegion(GuestAddress from, GuestAddress to) = 0;

private:
    std::mutex mutex_;
    std::map<GuestAddress, std::shared_ptr<const JITBlock>> blocks_;
};
