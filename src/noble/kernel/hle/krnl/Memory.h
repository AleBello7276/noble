#pragma once

#include "kernel/hle/Exports.h"

namespace hle::krnl {

// expose the typed virtual memory implementations for kernel export registration
std::span<const Export> MemoryExports();

}  // namespace hle::krnl
