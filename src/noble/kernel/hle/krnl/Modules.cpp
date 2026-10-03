#include "Modules.h"

#include "kernel/Kernel.h"
#include <array>

namespace hle::krnl {

// return whether the executable permits the requested system flag bit
uint32_t XexCheckExecutablePrivilege(Kernel& kernel, uint32_t privilege) {
    return kernel.CheckExecutablePrivilege(privilege);
}

constexpr std::array exports{
    Bind<&XexCheckExecutablePrivilege>(XboxLibrary::XboxKrnl, "XexCheckExecutablePrivilege"),
};

std::span<const Export> ModuleExports() {
    return exports;
}

}  // namespace hle::krnl
