#pragma once

#include <cstdint>

namespace gpu {

// emulator command consumed when the ring buffer reaches a presentation request
inline constexpr uint32_t kSwapOpcode = 0x64;
inline constexpr uint32_t kSwapSignature = 0x53574150;

// encode a sequential register write with count between 1 and 0x4000
constexpr uint32_t MakePacketType0(uint32_t index, uint32_t count) {
    return ((count - 1) << 16) | index;
}

// encode a one word nop packet
constexpr uint32_t MakePacketType2() {
    return 0x80000000;
}

// encode an unpredicated command with count between 1 and 0x4000
constexpr uint32_t MakePacketType3(uint32_t opcode, uint32_t count) {
    return 0xC0000000 | ((count - 1) << 16) | (opcode << 8);
}

}  // namespace gpu
