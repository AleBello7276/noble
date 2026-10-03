#pragma once

#include "kernel/hle/Exports.h"

namespace hle::krnl {

// expose the typed executable and module implementations for kernel export registration
std::span<const Export> ModuleExports();

}  // namespace hle::krnl
