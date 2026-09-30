#include "Threading.h"

#include "kernel/Kernel.h"
#include <array>

namespace hle::krnl {
namespace {

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
    Bind<&KeTlsAlloc>(XboxLibrary::XboxKrnl, "KeTlsAlloc"),
    Bind<&KeTlsFree>(XboxLibrary::XboxKrnl, "KeTlsFree"),
    Bind<&KeTlsGetValue>(XboxLibrary::XboxKrnl, "KeTlsGetValue"),
    Bind<&KeTlsSetValue>(XboxLibrary::XboxKrnl, "KeTlsSetValue"),
};

}  // namespace

std::span<const Export> ThreadingExports() {
    return exports;
}

}  // namespace hle::krnl
