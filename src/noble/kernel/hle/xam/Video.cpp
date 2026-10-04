#include "Video.h"

#include "kernel/hle/krnl/Video.h"
#include <array>

namespace hle::xam {

// report the same video mode as the kernel query
void XGetVideoMode(Pointer<krnl::VideoMode> videoMode) {
    krnl::QueryVideoMode(*videoMode);
}

constexpr std::array exports{
    Bind<&XGetVideoMode>(XboxLibrary::Xam, "XGetVideoMode"),
};

std::span<const Export> VideoExports() {
    return exports;
}

}  // namespace hle::xam
