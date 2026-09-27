#include "kernel.h"

#include "Logger.h"
#include "cpu/Scheduler.h"
#include "emulator/Memory.h"
#include <assert.h>

Kernel::Kernel(Memory& memory, Scheduler& scheduler) : memory_(memory), scheduler_(scheduler) {}

bool Kernel::Initialize() {
    next_process_id_ = 1;
    next_thread_id_ = 1;

    return true;
}

void Kernel::Shutdown() {
    processes_.clear();
}

KProcess* Kernel::CreateProcess(const ProcessCreateInfo& info) {
    auto process = std::make_unique<KProcess>(next_process_id_++);

    process->mImageBase_ = info.image_base;
    process->mEntryPoint_ = info.entry_point;

    KProcess* result = process.get();

    processes_.push_back(std::move(process));

    return result;
}

bool Kernel::AllocateThreadStack(KThread& thread, uint32_t size) {
    assert(false);
    // size = AlignUp(size, 0x1000);
    //// GuestAddress base = memory_.AllocateVirtual(size);
    //
    // if (!base) {
    //    LOG_FATAL("Kernel::AllocateThreadStack -> Unable to allocate Thread stack\n");
    //    assert(base);
    //    return false;
    //}
    //
    // thread.mStackLimit = base;
    // thread.mStackBase = base + size;
    //
    return true;
}

void Kernel::InitializeThreadContext(KThread& thread, const ThreadCreateInfo& info) {
    thread.mContext = {};

    thread.mContext.CIA = info.entry_point;

    /* PPC stack pointer = r1 */
    thread.mContext.GPRs[1] = (GPR)thread.mStackBase;

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
    assert(thread);

    if (!thread)
        return;

    if (thread->mState == ThreadState::Suspended) {
        return;
    }

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

    thread->exit_code = exit_code;

    thread->mState = ThreadState::Terminated;
}

void Kernel::ExitProcess(KProcess* process, uint32_t exit_code) {
    if (!process)
        return;

    process->terminated_ = true;

    process->exit_code_ = exit_code;

    for (auto& thread : process->threads_) {
        if (thread->mState != ThreadState::Terminated) {
            scheduler_.TerminateThread(thread.get(), exit_code);
        }
    }
}
