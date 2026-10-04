#pragma once

#include "kernel/hle/Exports.h"

namespace hle::xam {

// expose the typed video implementations for xam export registration
std::span<const Export> VideoExports();

}  // namespace hle::xam
