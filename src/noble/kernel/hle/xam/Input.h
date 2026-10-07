#pragma once

#include "core/endian.h"
#include "kernel/hle/Exports.h"
#include <cstddef>

namespace hle::xam {

// guest gamepad buttons, triggers and signed stick axes in xbox byte order
struct InputGamepad {
    be<uint16_t> buttons;
    uint8_t leftTrigger;
    uint8_t rightTrigger;
    be<int16_t> thumbLX;
    be<int16_t> thumbLY;
    be<int16_t> thumbRX;
    be<int16_t> thumbRY;
};
static_assert(sizeof(InputGamepad) == 12);
static_assert(offsetof(InputGamepad, thumbLX) == 4);

// packet number changes when the reported controller state changes
struct InputState {
    be<uint32_t> packetNumber;
    InputGamepad gamepad;
};
static_assert(sizeof(InputState) == 16);
static_assert(offsetof(InputState, gamepad) == 4);

// report a disconnected controller until an input backend is available
// a null state pointer may be used to query connection status
uint32_t XamInputGetState(uint32_t userIndex, uint32_t flags,
                          Pointer<InputState, PointerValidation::Report> inputState);

// expose the typed input implementations for xam export registration
std::span<const Export> InputExports();

}  // namespace hle::xam
