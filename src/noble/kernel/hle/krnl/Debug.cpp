#include "Debug.h"

#include <array>

#ifdef _WIN32
#include <intrin.h>
#else
#include <csignal>
#endif

namespace hle::krnl {

void DbgBreakPoint() {
#ifdef _WIN32
    __debugbreak();
#else
    std::raise(SIGTRAP);
#endif
}

constexpr std::array exports{
    Bind<&DbgBreakPoint>(XboxLibrary::XboxKrnl, "DbgBreakPoint"),
};

std::span<const Export> DebugExports() {
    return exports;
}

}  // namespace hle::krnl
