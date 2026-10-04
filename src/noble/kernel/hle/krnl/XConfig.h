#pragma once

#include "kernel/hle/Exports.h"

namespace hle::krnl {

// expose console configuration reads with emulator defaults and guest byte layouts
std::span<const Export> XConfigExports();

}  // namespace hle::krnl
