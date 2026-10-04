#include "Video.h"

#include "kernel/hle/krnl/Video.h"
#include <array>

namespace hle::xam {

// report the same video mode as the kernel query
void XGetVideoMode(Kernel& kernel, Pointer<krnl::VideoMode> videoMode) {
    krnl::QueryVideoMode(kernel, *videoMode);
}

constexpr std::array exports{
    Bind<&XGetVideoMode>(XboxLibrary::Xam, "XGetVideoMode"),
};

std::span<const Export> VideoExports() {
    return exports;
}

}  // namespace hle::xam
