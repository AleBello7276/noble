#pragma once

#include "kernel/hle/Exports.h"

namespace hle::krnl {

// expose object handle and reference services for kernel export registration
std::span<const Export> ObjectExports();

}  // namespace hle::krnl
