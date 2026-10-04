#include "Threading.h"

#include "core/HostClock.h"
#include "core/byte_swap.h"
#include "kernel/KThread.h"
#include "kernel/Kernel.h"
#include <algorithm>
#include <array>

#include "Logger.h"

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

void KiApcNormalRoutineNop() {}

void KeEnterCriticalRegion(KThread& thread) {
    thread.EnterCriticalRegion();
}

void KeLeaveCriticalRegion(KThread& thread) {
    thread.LeaveCriticalRegion();
}

void KeInitializeDpc(Pointer<XDPC> dpc, GuestAddress routine, GuestAddress context) {
    dpc->Initialize(routine, context);
}

uint32_t KeWaitForSingleObject(Kernel& kernel, KThread& thread, PPCContext& cpu,
                               Pointer<X_DISPATCH_HEADER, PointerValidation::Report> object, uint32_t reason,
                               uint32_t mode, uint32_t alertable,
                               Pointer<const be<int64_t>, PointerValidation::Report> timeout) {
    if (!object || (timeout.guest_address() && !timeout))
        return 0xC0000005;

    return kernel.WaitForSingleObject(thread, cpu, object.guest_address(), reason, mode, alertable != 0,
                                      timeout ? std::optional<int64_t>(int64_t(*timeout)) : std::nullopt);
}

void KeInitializeEvent(Kernel& kernel, Pointer<X_KEVENT> event, uint32_t type, uint32_t state) {
    kernel.InitializeEvent(event.guest_address(), type, state != 0);
}

uint32_t KeSetEvent(Kernel& kernel, Pointer<X_KEVENT> event, uint32_t increment, uint32_t wait) {
    return kernel.SetEvent(event.guest_address());
}

uint32_t KeResetEvent(Kernel& kernel, Pointer<X_KEVENT> event) {
    return kernel.ResetEvent(event.guest_address());
}

// create a guest thread and publish its outputs before making it runnable
uint32_t ExCreateThread(Kernel& kernel, Memory& memory, KThread& caller,
                        Pointer<be<uint32_t>, PointerValidation::Report> handle_ptr, uint32_t stack_size,
                        Pointer<be<uint32_t>, PointerValidation::Report> thread_id_ptr,
                        uint32_t xapi_thread_startup, uint32_t start_address, uint32_t start_context,
                        uint32_t creation_flags) {
    constexpr uint32_t invalidParameter = 0xC000000D;
    constexpr uint32_t accessViolation = 0xC0000005;
    constexpr uint32_t noMemory = 0xC0000017;
    constexpr uint32_t processTerminating = 0xC000010A;

    if ((handle_ptr.guest_address() && !handle_ptr) || (thread_id_ptr.guest_address() && !thread_id_ptr))
        return accessViolation;

    const uint32_t affinity = creation_flags >> 24;
    if ((affinity & ~kAllProcessors) || !start_address || start_address % 4 || xapi_thread_startup % 4)
        return invalidParameter;

    if (!memory.IsAccessible(start_address, 4)
        || (xapi_thread_startup && !memory.IsAccessible(xapi_thread_startup, 4)))
        return accessViolation;

    try {
        KProcess* process = kernel.GetThreadProcess((creation_flags & SystemThread) != 0);
        if (!process)
            return noMemory;

        if (process->terminated())
            return processTerminating;

        const auto* record = static_cast<const GuestKernelProcess*>(
            memory.Translate(process->guest_address(), sizeof(GuestKernelProcess)));
        const uint64_t requested = stack_size ? stack_size : byte_swap(record->kernelStackSize);
        const uint64_t actual = std::max<uint64_t>(0x4000, AlignUp(requested, 4096));

        if (actual > UINT32_MAX)
            return invalidParameter;

        ThreadCreateInfo info;
        info.entry_point = start_address;
        info.parameter = start_context;
        info.startup = xapi_thread_startup;
        info.creation_flags = creation_flags;
        info.handle_process = caller.process();
        info.stack_size = static_cast<uint32_t>(actual);
        info.affinity_mask = affinity ? static_cast<uint8_t>(affinity) : kAllProcessors;
        info.priority = record->defaultPriority;
        info.create_suspended = (creation_flags & ThreadInitiallySuspended) != 0;
        KThread* thread = kernel.CreateThread(process, info);

        if (!thread)
            return noMemory;

        if (handle_ptr)
            *handle_ptr = (creation_flags & ReturnKThreadPtr) ? thread->guest_address() : thread->handle();

        if (thread_id_ptr)
            *thread_id_ptr = thread->id();

        if (!info.create_suspended)
            kernel.StartThread(thread);

        return 0;
    } catch (const std::bad_alloc&) {
        return noMemory;
    }
}

int32_t KeSetBasePriorityThread(Kernel& kernel, GuestAddress thread_address, int32_t increment) {
    auto* thread = dynamic_cast<KThread*>(kernel.LookupGuestObject(thread_address));
    if (!thread) {
        LOG_WARN("KeSetBasePriorityThread received an invalid thread 0x{:08X}", thread_address);
        return 0;
    }
    return kernel.SetBasePriorityThread(thread, increment);
}

constexpr std::array exports{
    Bind<&KeGetCurrentProcessType>(XboxLibrary::XboxKrnl, "KeGetCurrentProcessType"),
    Bind<&KeEnterCriticalRegion>(XboxLibrary::XboxKrnl, "KeEnterCriticalRegion"),
    Bind<&KeLeaveCriticalRegion>(XboxLibrary::XboxKrnl, "KeLeaveCriticalRegion"),
    Bind<&KeTlsAlloc>(XboxLibrary::XboxKrnl, "KeTlsAlloc"),
    Bind<&KeTlsFree>(XboxLibrary::XboxKrnl, "KeTlsFree"),
    Bind<&KeTlsGetValue>(XboxLibrary::XboxKrnl, "KeTlsGetValue"),
    Bind<&KeTlsSetValue>(XboxLibrary::XboxKrnl, "KeTlsSetValue"),
    Bind<&KeQueryPerformanceFrequency>(XboxLibrary::XboxKrnl, "KeQueryPerformanceFrequency"),
    Bind<&KiApcNormalRoutineNop>(XboxLibrary::XboxKrnl, "KiApcNormalRoutineNop"),
    Bind<&KiApcNormalRoutineNop>(XboxLibrary::XboxKrnl, "KiApcNormalRoutineNop_"),
    Bind<&KeInitializeDpc>(XboxLibrary::XboxKrnl, "KeInitializeDpc"),
    Bind<&KeWaitForSingleObject>(XboxLibrary::XboxKrnl, "KeWaitForSingleObject"),
    Bind<&KeInitializeEvent>(XboxLibrary::XboxKrnl, "KeInitializeEvent"),
    Bind<&KeSetEvent>(XboxLibrary::XboxKrnl, "KeSetEvent"),
    Bind<&KeResetEvent>(XboxLibrary::XboxKrnl, "KeResetEvent"),
    Bind<&ExCreateThread>(XboxLibrary::XboxKrnl, "ExCreateThread"),
    Bind<&KeSetBasePriorityThread>(XboxLibrary::XboxKrnl, "KeSetBasePriorityThread"),
};

std::span<const Export> ThreadingExports() {
    return exports;
}

}  // namespace hle::krnl
