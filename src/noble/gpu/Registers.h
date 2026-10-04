#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace gpu {

// cpu visible gpu register window as mapped by xenia graphics_system
inline constexpr uint32_t kRegisterBase = 0x7FC80000;
inline constexpr uint32_t kRegisterWindowSize = 0x10000;
inline constexpr uint32_t kMMIORegisterCount = kRegisterWindowSize / sizeof(uint32_t);
// command packets can reference registers beyond the cpu mmio window
inline constexpr uint32_t kRegisterCount = 0x5003;

// values are dword indices rather than guest addresses or byte offsets
enum class Register : uint32_t {
#define NOBLE_GPU_REGISTER(index, type, name) name = index,
#include "RegisterTable.inc"
#undef NOBLE_GPU_REGISTER
    // special read handled by xenia but absent from its register table
    RB_EDRAM_TIMING = 0x0F00,
};

enum class RegisterType { kDword, kFloat };

struct RegisterInfo {
    Register index;
    RegisterType type;
    std::string_view name;
};

// convert a register index to its offset within the register file
constexpr uint32_t RegisterByteOffset(Register index) {
    return static_cast<uint32_t>(index) * sizeof(uint32_t);
}

// check whether the register can be accessed through the cpu mmio window
constexpr bool IsMMIORegister(Register index) {
    return static_cast<uint32_t>(index) < kMMIORegisterCount;
}

// return the guest address only for registers exposed through cpu mmio
constexpr std::optional<uint32_t> RegisterAddress(Register index) {
    if (!IsMMIORegister(index))
        return std::nullopt;
    return kRegisterBase + RegisterByteOffset(index);
}

// decode an aligned address within the cpu gpu register window
constexpr std::optional<Register> RegisterFromAddress(uint32_t address) {
    if (address < kRegisterBase || address - kRegisterBase >= kRegisterWindowSize || address % 4)
        return std::nullopt;
    return static_cast<Register>((address - kRegisterBase) / sizeof(uint32_t));
}

// look up a documented register including its raw word or float interpretation
// unknown indices return no metadata even when storage exists for them
constexpr std::optional<RegisterInfo> GetRegisterInfo(uint32_t index) {
    switch (index) {
#define NOBLE_GPU_REGISTER(index, type, name)                                                                \
    case index:                                                                                              \
        return RegisterInfo{Register::name, RegisterType::type, #name};
#include "RegisterTable.inc"
#undef NOBLE_GPU_REGISTER
    case static_cast<uint32_t>(Register::RB_EDRAM_TIMING):
        return RegisterInfo{Register::RB_EDRAM_TIMING, RegisterType::kDword, "RB_EDRAM_TIMING"};
    default:
        return std::nullopt;
    }
}

// look up metadata using a named register
constexpr std::optional<RegisterInfo> GetRegisterInfo(Register index) {
    return GetRegisterInfo(static_cast<uint32_t>(index));
}

// check whether xenia provides a name for the register
constexpr bool IsKnownRegister(uint32_t index) {
    return GetRegisterInfo(index).has_value();
}

static_assert(RegisterByteOffset(Register::CP_PROG_COUNTER) == 0x112C);
static_assert(RegisterAddress(Register::CP_PROG_COUNTER) == 0x7FC8112C);
static_assert(!RegisterAddress(Register::SHADER_CONSTANT_FLUSH_FETCH_2));

}  // namespace gpu
