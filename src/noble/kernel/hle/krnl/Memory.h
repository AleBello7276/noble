#pragma once

#include "kernel/hle/Exports.h"

namespace hle::krnl {

std::span<const Export> MemoryExports();

}  // namespace hle::krnl
