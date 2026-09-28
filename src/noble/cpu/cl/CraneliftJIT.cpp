#include "CraneliftJIT.h"

#include "powerpc-rs.h"

CraneliftJIT::CraneliftJIT(Memory& memory) : memory_(memory), jit_module_(cranelift::JITBuilder()) {}

void CraneliftJIT::CompilePPCModule(PPCModule& module) {}

void CraneliftJIT::CompileJITBlock(GuestAddress address) {}

void CraneliftJIT::InvalidateBlock(GuestAddress address) {}

void CraneliftJIT::InvalidateRegion(GuestAddress from, GuestAddress to) {}

JITBlock CraneliftJIT::FindBlock(GuestAddress address) const {
    return NULL;
}
