#include "Logger.h"
#include "Scheduler.h"
#include "diagnostics/TraceEvents.h"

#include <algorithm>
#include <assert.h>

// evnetually move this stuff under a platform agnostic abstraction

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <pthread.h>
#endif

void NameWorkerThread(HWT_ID processor_id) {
#if defined(_WIN32)

    const auto name = std::format(L"noble CPU {}", processor_id);
    const HRESULT result = SetThreadDescription(GetCurrentThread(), name.c_str());
    if (FAILED(result))
        LOG_WARN("failed to name CPU {} worker: 0x{:08X}", processor_id, uint32_t(result));

#elif defined(__linux__) || defined(__APPLE__)

    const auto name = std::format("noble CPU {}", processor_id);

#if defined(__APPLE__)

    const int result = pthread_setname_np(name.c_str());

#else

    const int result = pthread_setname_np(pthread_self(), name.c_str());

#endif

    if (result != 0)
        LOG_WARN("failed to name CPU {} worker: {}", processor_id, result);

#endif
}

thread_local HardwareThread* gCurrentProcessor = nullptr;

Scheduler::Scheduler(CpuExecutor& cpu, diagnostics::TraceSink* trace) : cpu_(cpu), trace_(trace) {
    for (uint32_t i = 0; i < kProcessorCount; ++i) {
        processors_[i].id = i;
    }
}

Scheduler::~Scheduler() {
    Stop();
}

bool Scheduler::Initialise() {
    return true;
}

void Scheduler::Start() {
    std::scoped_lock lifecycle_lock(lifecycle_mutex_);
    {
        std::scoped_lock lock(mutex_);

        if (started_)
            return;

        started_ = true;
    }

    for (HWT_ID i = 0; i < kProcessorCount; ++i) {
        workers_[i] = std::jthread([this, i](std::stop_token stop_token) { WorkerMain(i, stop_token); });
    }
}

void Scheduler::Stop() {
    std::scoped_lock lifecycle_lock(lifecycle_mutex_);
    {
        std::scoped_lock lock(mutex_);
        if (!started_) {
            ready_queue_.clear();
            return;
        }
        started_ = false;
    }
    // request all workers a stop
    for (auto& worker : workers_)
        worker.request_stop();

    // wake up all workers so they actually stop
    cv_.notify_all();

    // join
    for (auto& worker : workers_) {
        if (worker.joinable())
            worker.join();
    }

    std::scoped_lock lock(mutex_);
    ready_queue_.clear();
}

bool Scheduler::HasRunnableThreadLocked(HWT_ID processor_id) const {
    const ThreadAffinity processor_bit = static_cast<ThreadAffinity>(1u << processor_id);

    /*
        hardware thread got woken up,
        check the queue for a valid guest thread to execute
    */
    for (auto thread : ready_queue_) {
        if (thread->mState == ThreadState::Ready && (thread->mAffinityMask & processor_bit))
            return true;
    }

    return false;
}

void Scheduler::MakeRunnable(KThread* thread) {
    if (!thread)
        return;

    // add thread to queue
    {
        std::scoped_lock lock(mutex_);

        if (thread->mState == ThreadState::Ready || thread->mState == ThreadState::Running
            || thread->mState == ThreadState::Terminated || thread->mState == ThreadState::Suspended
            || thread->mAffinityMask == 0)
            return;

        thread->mState = ThreadState::Ready;
        thread->SyncGuestState();
        ready_queue_.push_back(thread);

        diagnostics::EmitThread(trace_, diagnostics::EventKind::ThreadReady, *thread);
    }

    // wake all threads
    cv_.notify_all();
}

KThread* Scheduler::PickNextThreadLocked(HWT_ID processor_id) {
    const ThreadAffinity processor_bit = static_cast<ThreadAffinity>(1u << processor_id);

    /*

    */

    auto selected = ready_queue_.end();
    for (auto it = ready_queue_.begin(); it != ready_queue_.end(); ++it) {
        KThread* thread = *it;

        if (thread->mState != ThreadState::Ready || !(thread->mAffinityMask & processor_bit))
            continue;

        if (selected == ready_queue_.end() || thread->mPriority > (*selected)->mPriority)
            selected = it;
    }

    if (selected == ready_queue_.end())
        return nullptr;

    KThread* thread = *selected;
    ready_queue_.erase(selected);
    return thread;
}

int32_t Scheduler::SetBasePriorityThread(KThread* thread, int32_t increment) {
    std::scoped_lock lock(mutex_);

    if (!thread || !thread->guestThread_ || !thread->process() || !thread->process()->guestProcess_)
        return 0;

    if (thread->process()->type() == ProcessType::Idle)
        return 0;

    auto& record = *thread->guestThread_;
    const int32_t processBase = thread->process()->guestProcess_->defaultPriority;
    const int32_t previous = record.saturationIncrement == 0xFF ? -16 :
                             record.saturationIncrement         ? 16 :
                                                                  thread->mBasePriority - processBase;
    const bool realtime = processBase >= 16;
    const auto priority = static_cast<int32_t>(
        std::clamp<int64_t>(int64_t(processBase) + increment, realtime ? 16 : 1, realtime ? 31 : 15));

    thread->mBasePriority = thread->mPriority = priority;
    record.basePriority = record.basePriorityCopy = record.priority = static_cast<uint8_t>(priority);
    record.saturationIncrement = increment <= -16 ? 0xFF : increment >= 16 ? 1 : 0;
    record.priorityDecrement = 0;

    diagnostics::EmitThread(trace_, diagnostics::EventKind::PriorityChanged, *thread);

    // the next scheduling decision observes the new priority without changing thread state
    cv_.notify_all();
    return previous;
}

void Scheduler::WorkerMain(HWT_ID processor_id, std::stop_token stop_token) {
    NameWorkerThread(processor_id);
    HardwareThread& processor = processors_[processor_id];
    gCurrentProcessor = &processor;

    while (!stop_token.stop_requested()) {
        KThread* thread = nullptr;

        {
            std::unique_lock lock(mutex_);

            // wait until valid guest thread availabe
            cv_.wait(lock, stop_token, [&] { return HasRunnableThreadLocked(processor_id); });

            if (stop_token.stop_requested())
                break;

            thread = PickNextThreadLocked(processor_id);

            if (!thread)
                continue;

            processor.current_thread = thread;

            thread->mState = ThreadState::Running;

            thread->mLastProcessor = thread->mCurrentProcessor;
            thread->mCurrentProcessor = processor_id;
            thread->SyncGuestState();

            diagnostics::EmitThread(trace_, diagnostics::EventKind::ThreadScheduled, *thread);
        }

        diagnostics::ScopedWorkerContext traceContext(thread->id(), processor_id);
        ExecutionResult result = cpu_.Execute(thread->mContext, thread->mTerminateRequested, stop_token);

        HandleExecutionResult(processor_id, thread, result);
    }

    gCurrentProcessor = nullptr;
}

void Scheduler::HandleExecutionResult(HWT_ID processor_id, KThread* thread, ExecutionResult result) {
    if (result.reason == ExecutionReason::Fault)
        LOG_ERROR("Guest thread {} faulted at 0x{:08X}", thread->id(), result.fault_address);
    {
        std::scoped_lock lock(mutex_);

        processors_[processor_id].current_thread = nullptr;
        thread->mCurrentProcessor = kInvalidProcessor;

        const bool termination_requested = thread->mTerminateRequested.load(std::memory_order_acquire);
        if (result.reason == ExecutionReason::Waiting && thread->mWaitResult) {
            thread->mContext.GPRs[3].s64 = std::bit_cast<int32_t>(*thread->mWaitResult);
            thread->mWaitResult.reset();
        }
        if (termination_requested || result.reason == ExecutionReason::Exited
            || result.reason == ExecutionReason::Fault) {
            thread->mState = ThreadState::Terminated;

            if (!termination_requested && result.reason == ExecutionReason::Fault) {
                thread->faulted = true;
                thread->exit_code = result.fault_address;
            }

        } else if (result.reason == ExecutionReason::Waiting && thread->mWaitPending) {
            thread->mState = ThreadState::Waiting;

        } else {
            thread->mState = ThreadState::Ready;
            ready_queue_.push_back(thread);
        }
        thread->SyncGuestState();

        if (result.reason == ExecutionReason::Fault) {
            diagnostics::Event event;
            event.kind = diagnostics::EventKind::Fault;
            event.thread = thread->id();
            event.cpu = processor_id;
            event.address = result.fault_address;

            if (trace_)
                trace_->Emit(event);
        }

        diagnostics::EmitThread(
            trace_,
            thread->mState == ThreadState::Terminated ? diagnostics::EventKind::ThreadTerminated :
            thread->mState == ThreadState::Waiting    ? diagnostics::EventKind::ThreadWaiting :
                                                        diagnostics::EventKind::ThreadReady,
            *thread);
    }

    // notify all
    cv_.notify_all();
}

bool Scheduler::PrepareWait(KThread* thread) {
    std::scoped_lock lock(mutex_);
    if (!thread || thread->mState == ThreadState::Terminated || thread->mWaitPending
        || thread->mTerminateRequested.load(std::memory_order_acquire))
        return false;
    thread->mWaitPending = true;
    thread->mWaitResult.reset();
    return true;
}

bool Scheduler::WakeThread(KThread* thread, std::optional<uint32_t> result,
                           const std::function<bool()>& acquire) {
    {
        std::scoped_lock lock(mutex_);
        if (!thread || !thread->mWaitPending || thread->mState == ThreadState::Terminated
            || thread->mTerminateRequested.load(std::memory_order_acquire))
            return false;

        if (acquire && !acquire())
            return false;

        if (result) {
            if (thread->mState == ThreadState::Running)
                thread->mWaitResult = result;
            else
                thread->mContext.GPRs[3].s64 = std::bit_cast<int32_t>(*result);
        }
        thread->mWaitPending = false;
        if (thread->mState == ThreadState::Waiting) {
            thread->mState = ThreadState::Ready;
            thread->SyncGuestState();
            ready_queue_.push_back(thread);

            diagnostics::EmitThread(trace_, diagnostics::EventKind::ThreadReady, *thread);
        }
        // a running worker handles an early wake when it processes the waiting result
    }
    cv_.notify_all();
    return true;
}

bool Scheduler::IsThreadTerminated(KThread* thread) {
    std::scoped_lock lock(mutex_);
    return thread && thread->mState == ThreadState::Terminated;
}

KThread* Scheduler::CurrentThread() const {
    if (!gCurrentProcessor)
        return nullptr;

    return gCurrentProcessor->current_thread;
}

void Scheduler::WaitForThread(KThread* thread) {
    if (!thread)
        return;
    {
        std::unique_lock lock(mutex_);

        cv_.wait(lock, [&] { return thread->mState == ThreadState::Terminated || !started_; });
    }
}

void Scheduler::TerminateThread(KThread* thread, uint32_t exitCode) {
    if (!thread)
        return;

    {
        // lock
        std::scoped_lock lock(mutex_);

        if (thread->mState == ThreadState::Terminated) {
            return;
        }

        thread->exit_code = exitCode;

        /* if in queue remove it now */
        if (thread->mState == ThreadState::Ready) {
            std::erase(ready_queue_, thread);

            thread->mState = ThreadState::Terminated;
            thread->mCurrentProcessor = kInvalidProcessor;
            thread->SyncGuestState();
            cv_.notify_all();

            diagnostics::EmitThread(trace_, diagnostics::EventKind::ThreadTerminated, *thread);
            return;
        }

        /*
            Created / suspended / waiting threads are not executing,
            so set them to Terminated
        */
        if (thread->mState == ThreadState::Created || thread->mState == ThreadState::Suspended
            || thread->mState == ThreadState::Waiting) {
            thread->mState = ThreadState::Terminated;

            thread->mCurrentProcessor = kInvalidProcessor;
            thread->SyncGuestState();

            cv_.notify_all();

            diagnostics::EmitThread(trace_, diagnostics::EventKind::ThreadTerminated, *thread);
            return;
        }

        /* worker is executing so dont change the state yet */
        if (thread->mState == ThreadState::Running) {
            thread->mTerminateRequested.store(true, std::memory_order_release);

            return;
        }
    }
}
