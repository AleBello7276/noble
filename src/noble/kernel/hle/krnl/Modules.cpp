#include "Modules.h"

#include "kernel/Kernel.h"
#include <array>

namespace hle::krnl {

void ExRegisterTitleTerminateNotification(Kernel& kernel,
                                          Pointer<const TitleTerminateRegistration> registration,
                                          uint32_t create) {
    const GuestAddress routine = registration->notificationRoutine;
    if (create)
        kernel.RegisterTitleTerminateNotification(routine, registration->priority);
    else
        kernel.RemoveTitleTerminateNotification(routine);
}

uint32_t XexCheckExecutablePrivilege(Kernel& kernel, uint32_t privilege) {
    return kernel.CheckExecutablePrivilege(privilege);
}

constexpr std::array exports{
    Bind<&ExRegisterTitleTerminateNotification>(XboxLibrary::XboxKrnl,
                                                "ExRegisterTitleTerminateNotification"),
    Bind<&XexCheckExecutablePrivilege>(XboxLibrary::XboxKrnl, "XexCheckExecutablePrivilege"),
};

std::span<const Export> ModuleExports() {
    return exports;
}

}  // namespace hle::krnl
