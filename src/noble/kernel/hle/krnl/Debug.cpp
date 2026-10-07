#include "Debug.h"

#include "Logger.h"
#include <array>

namespace hle::krnl {

void DbgBreakPoint(PPCContext& cpu) {
    LOG_ERROR("Guest DbgBreakPoint at {:08X} (caller return {:08X})\n", cpu.CIA, uint32_t(cpu.SPRs.LR));
    cpu.Action = HostAction::DebugBreak;
}

constexpr std::array exports{
    Bind<&DbgBreakPoint>(XboxLibrary::XboxKrnl, "DbgBreakPoint"),
};

std::span<const Export> DebugExports() {
    return exports;
}

}  // namespace hle::krnl
