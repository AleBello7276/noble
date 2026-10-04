#include "Threading.h"

#include "core/HostClock.h"
#include "kernel/Kernel.h"
#include <array>

namespace hle::krnl {

// get the process type inherited by the calling guest thread
uint32_t KeGetCurrentProcessType(KThread& thread) {
    if (!thread.process())
        throw std::runtime_error("guest thread has no process");

    return static_cast<uint32_t>(thread.process()->type());
}

// allocate a process tls slot and initialize the calling thread value
uint32_t KeTlsAlloc(Kernel& kernel, KThread& thread) {
    return kernel.AllocateTLS(thread);
}

// release a tls slot and clear its value in every thread of the process
uint32_t KeTlsFree(Kernel& kernel, KThread& thread, uint32_t index) {
    return kernel.FreeTLS(thread, index);
}

// read the calling thread value from its guest tls storage
uint32_t KeTlsGetValue(Kernel& kernel, KThread& thread, uint32_t index) {
    return kernel.GetTLSValue(thread, index);
}

// update the calling thread value in its guest tls storage
uint32_t KeTlsSetValue(Kernel& kernel, KThread& thread, uint32_t index, uint32_t value) {
    return kernel.SetTLSValue(thread, index, value);
}

uint32_t KeQueryPerformanceFrequency() {
    uint64_t result = HostClock::GetInstance().GetGuestTickFreq();
    return static_cast<uint32_t>(result);
}

// accept normal apc callbacks that intentionally perform no work
void KiApcNormalRoutineNop() {}

constexpr std::array exports{
    Bind<&KeGetCurrentProcessType>(XboxLibrary::XboxKrnl, "KeGetCurrentProcessType"),
    Bind<&KeTlsAlloc>(XboxLibrary::XboxKrnl, "KeTlsAlloc"),
    Bind<&KeTlsFree>(XboxLibrary::XboxKrnl, "KeTlsFree"),
    Bind<&KeTlsGetValue>(XboxLibrary::XboxKrnl, "KeTlsGetValue"),
    Bind<&KeTlsSetValue>(XboxLibrary::XboxKrnl, "KeTlsSetValue"),
    Bind<&KeQueryPerformanceFrequency>(XboxLibrary::XboxKrnl, "KeQueryPerformanceFrequency"),
    Bind<&KiApcNormalRoutineNop>(XboxLibrary::XboxKrnl, "KiApcNormalRoutineNop"),
    Bind<&KiApcNormalRoutineNop>(XboxLibrary::XboxKrnl, "KiApcNormalRoutineNop_"),
};

std::span<const Export> ThreadingExports() {
    return exports;
}

}  // namespace hle::krnl
