#include "Input.h"

#include <array>

namespace hle::xam {

uint32_t XamInputGetState([[maybe_unused]] uint32_t userIndex,
                          [[maybe_unused]] uint32_t flags,
                          [[maybe_unused]] Pointer<InputState, PointerValidation::Report> inputState) {
    // no input devices are connected and failed queries leave the output untouched
    constexpr uint32_t errorDeviceNotConnected = 1167;
    return errorDeviceNotConnected;
}

constexpr std::array exports{
    Bind<&XamInputGetState>(XboxLibrary::Xam, "XamInputGetState"),
};

std::span<const Export> InputExports() {
    return exports;
}

}  // namespace hle::xam
