#pragma once

#include "kernel/hle/Exports.h"

namespace hle::krnl {

// raise a host debugger breakpoint and resume when the debugger continues execution
void DbgBreakPoint();

// expose the typed debugging implementations for kernel export registration
std::span<const Export> DebugExports();

}  // namespace hle::krnl
