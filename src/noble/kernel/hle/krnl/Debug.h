#pragma once

#include "kernel/hle/Exports.h"

namespace hle::krnl {

// request a guest debugger stop after this shim returns to the dispatcher
void DbgBreakPoint(PPCContext& cpu);

// expose the typed debugging implementations for kernel export registration
std::span<const Export> DebugExports();

}  // namespace hle::krnl
