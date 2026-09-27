#include "Kernel.h"

#include "Logger.h"
#include "cpu/Scheduler.h"
#include "emulator/Memory.h"
#include <algorithm>

Kernel::Kernel(Memory& memory, Scheduler& scheduler) : memory_(memory), scheduler_(scheduler) {}

bool Kernel::Initialize() {
    next_process_id_ = 1;
    next_thread_id_ = 1;

    return true;
}

void Kernel::Shutdown() {
    for (auto& process : processes_)
        for (auto& thread : process->threads_)
            if (thread->mStackLimit)
                memory_.FreeVirtual(thread->mStackLimit);

    processes_.clear();
}

KProcess* Kernel::CreateGuestProcess(const ProcessCreateInfo& info) {
    auto process = std::make_unique<KProcess>(next_process_id_++);

    process->mImageBase_ = info.image_base;
    process->mEntryPoint_ = info.entry_point;

    KProcess* result = process.get();

    processes_.push_back(std::move(process));

    return result;
}

bool Kernel::AllocateThreadStack(KThread& thread, uint32_t size) {
    if (size == 0 || (uint64_t)size + 4095 > UINT32_MAX)
        return false;

    size = AlignUp(size, 0x1000);
    const uint32_t base = memory_.AllocateVirtual(size);

    if (!base) {
        LOG_ERROR("Unable to allocate guest thread stack");
        return false;
    }

    thread.mStackLimit = base;
    thread.mStackBase = base + size;
    return true;
}

void Kernel::InitializeThreadContext(KThread& thread, const ThreadCreateInfo& info) {
    thread.mContext = {};

    thread.mContext.CIA = info.entry_point;

    /* PPC stack pointer = r1 */
    thread.mContext.GPRs[1].u64 = thread.mStackBase;
    // thread.mContext.GPRs[3].u64 = info.parameter;

    // TODO: arguments loading

    thread.mAffinityMask = info.affinity_mask;
    thread.mPriority = info.priority;
    thread.mState = ThreadState::Created;
}

KThread* Kernel::CreateThread(KProcess* process, const ThreadCreateInfo& info) {
    if (!process)
        return nullptr;

    auto thread = std::make_unique<KThread>(next_thread_id_++, process);

    if (!AllocateThreadStack(*thread, info.stack_size)) {
        return nullptr;
    }

    InitializeThreadContext(*thread, info);

    thread->mState = info.create_suspended ? ThreadState::Suspended : ThreadState::Created;

    KThread* result = thread.get();
    process->threads_.push_back(std::move(thread));

    return result;
}

void Kernel::StartThread(KThread* thread) {
    if (!thread)
        return;

    // add to queue
    scheduler_.MakeRunnable(thread);
}

KThread* Kernel::CreateInitialThread(KProcess* process) {
    ThreadCreateInfo info{};

    info.entry_point = process->mEntryPoint_;
    info.parameter = 0;
    info.stack_size = 1024 * 1024;

    info.affinity_mask = 1 << 0;
    info.priority = 0;
    info.create_suspended = false;

    return CreateThread(process, info);
}

KThread* Kernel::CurrentThread() {
    return scheduler_.CurrentThread();
}

KProcess* Kernel::CurrentProcess() {
    KThread* thread = CurrentThread();

    if (!thread)
        return nullptr;

    return thread->process();
}

void Kernel::ExitThread(KThread* thread, uint32_t exit_code) {
    if (!thread)
        return;

    scheduler_.TerminateThread(thread, exit_code);
}

void Kernel::ExitProcess(KProcess* process, uint32_t exit_code) {
    if (!process)
        return;

    process->terminated_ = true;

    process->exit_code_ = exit_code;

    for (auto& thread : process->threads_) {
        scheduler_.TerminateThread(thread.get(), exit_code);
    }
}
