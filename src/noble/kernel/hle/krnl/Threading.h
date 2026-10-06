#pragma once

#include "kernel/KernelTypes.h"
#include "kernel/hle/Exports.h"

namespace hle::krnl {

// https://github.com/xenia-canary/xenia-canary/blob/canary_experimental/src/xenia/kernel/xboxkrnl/xboxkrnl_threading.h#L77
enum CreateThreadFlags : uint32_t {
    ThreadInitiallySuspended = 0x00000001,
    SystemThread = 0x00000002,
    TLSStatic = 0x00000008,
    PriorityClass1 = 0x00000020,
    PriorityClass2 = 0x00000040,
    ReturnKThreadPtr = 0x00000080,
    TitleExecutionThread = 0x00000100,
    Hidden = 0x00000400,
    AffinityCpu0 = 0x01000000,
    AffinityCpu1 = 0x02000000,
    AffinityCpu2 = 0x04000000,
    AffinityCpu3 = 0x08000000,
    AffinityCpu4 = 0x10000000,
    AffinityCpu5 = 0x20000000,
};

XNTSTATUS KeSetAffinityThread(Kernel& kernel, KThread& caller, PPCContext& cpu, GuestAddress threadAddress,
                              uint32_t affinity,
                              Pointer<be<uint32_t>, PointerValidation::Report> previousAffinity);

// update the calling thread flag without delivering floating point exceptions
void KeEnableFpuExceptions(KThread& thread, uint32_t enabled);

// expose the typed threading implementations for kernel export registration
std::span<const Export> ThreadingExports();

}  // namespace hle::krnl
