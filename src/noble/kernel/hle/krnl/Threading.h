#pragma once

#include "kernel/hle/Exports.h"

namespace hle::krnl {

// expose the typed threading implementations for kernel export registration
std::span<const Export> ThreadingExports();

}  // namespace hle::krnl
