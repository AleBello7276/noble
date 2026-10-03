#pragma once

#include "kernel/hle/Exports.h"

namespace hle::krnl {

// expose the typed runtime library implementations for kernel export registration
std::span<const Export> RtlExports();

}  // namespace hle::krnl
