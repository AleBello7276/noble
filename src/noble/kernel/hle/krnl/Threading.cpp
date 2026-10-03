#include "Threading.h"

#include "kernel/Kernel.h"
#include <array>

namespace hle::krnl {

uint32_t KeGetCurrentProcessType(KThread& thread) {
    if (!thread.process())
        throw std::runtime_error("guest thread has no process");

    return static_cast<uint32_t>(thread.process()->type());
}

uint32_t KeTlsAlloc(Kernel& kernel, KThread& thread) {
    return kernel.AllocateTLS(thread);
}

uint32_t KeTlsFree(Kernel& kernel, KThread& thread, uint32_t index) {
    return kernel.FreeTLS(thread, index);
}

uint32_t KeTlsGetValue(Kernel& kernel, KThread& thread, uint32_t index) {
    return kernel.GetTLSValue(thread, index);
}

uint32_t KeTlsSetValue(Kernel& kernel, KThread& thread, uint32_t index, uint32_t value) {
    return kernel.SetTLSValue(thread, index, value);
}

constexpr std::array exports{
    Bind<&KeGetCurrentProcessType>(XboxLibrary::XboxKrnl, "KeGetCurrentProcessType"),
    Bind<&KeTlsAlloc>(XboxLibrary::XboxKrnl, "KeTlsAlloc"),
    Bind<&KeTlsFree>(XboxLibrary::XboxKrnl, "KeTlsFree"),
    Bind<&KeTlsGetValue>(XboxLibrary::XboxKrnl, "KeTlsGetValue"),
    Bind<&KeTlsSetValue>(XboxLibrary::XboxKrnl, "KeTlsSetValue"),
};

std::span<const Export> ThreadingExports() {
    return exports;
}

}  // namespace hle::krnl
