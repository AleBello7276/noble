#include "Scheduler.h"

#include <assert.h>

thread_local HardwareThread* gCurrentProcessor = nullptr;

Scheduler::Scheduler() {
    for (uint32_t i = 0; i < kProcessorCount; ++i) {
        processors_[i].id = i;
    }
}

void Scheduler::Start() {
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

    started_ = false;
}

bool Scheduler::HasRunnableThreadLocked(HWT_ID processor_id) const {
    const ThreadAffinity processor_bit = static_cast<ThreadAffinity>(1u << processor_id);

    /*
        hardware thread got woken up,
        check the queue for a valid guest thread to execute
    */
    for (auto thread : ready_queue_) {
        if (thread->mAffinityMask & processor_bit)
            return true;
    }

    return false;
}

void Scheduler::MakeRunnable(KThread* thread) {
    // add thread to queue
    {
        std::scoped_lock lock(mutex_);

        assert(thread->mState != ThreadState::Running);

        thread->mState = ThreadState::Ready;
        ready_queue_.push_back(thread);
    }

    // wake all threads
    cv_.notify_all();
}

KThread* Scheduler::PickNextThreadLocked(HWT_ID processor_id) {
    const ThreadAffinity processor_bit = static_cast<ThreadAffinity>(1u << processor_id);

    /*

    */

    for (auto it = ready_queue_.begin(); it != ready_queue_.end(); ++it) {
        KThread* thread = *it;

        if (!(thread->mAffinityMask & processor_bit))
            continue;

        ready_queue_.erase(it);

        return thread;
    }

    return nullptr;
}

void Scheduler::WorkerMain(HWT_ID processor_id, std::stop_token stop_token) {
    HardwareThread& processor = processors_[processor_id];
    gCurrentProcessor = &processor;

    while (!stop_token.stop_requested()) {
        KThread* thread = nullptr;

        {
            std::unique_lock lock(mutex_);

            // wait until valid guest thread availabe
            cv_.wait(lock, stop_token, [&] {
                ;
                return HasRunnableThreadLocked(processor_id);
            });

            if (stop_token.stop_requested())
                break;

            thread = PickNextThreadLocked(processor_id);

            if (!thread)
                continue;

            processor.current_thread = thread;

            thread->mState = ThreadState::Running;

            thread->mLastProcessor = thread->mCurrentProcessor;
            thread->mCurrentProcessor = processor_id;
        }

        ExecutionResult result = cpu_.Execute(thread->context, ExecutionBudget{.instructions = quantum_});

        HandleExecutionResult(processor_id, thread, result);
    }

    gCurrentProcessor = nullptr;
}

KThread* Scheduler::CurrentThread() const {
    if (!gCurrentProcessor)
        return nullptr;

    return gCurrentProcessor->current_thread;
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
            auto it = std::find(ready_queue_.begin(), ready_queue_.end(), thread);

            if (it != ready_queue_.end())
                ready_queue_.erase(it);

            thread->mState = ThreadState::Terminated;
            thread->mCurrentProcessor = kInvalidProcessor;
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

            return;
        }

        /* worker is executing so dont change the state yet */
        if (thread->mState == ThreadState::Running) {
            thread->mTerminateRequested.store(true, std::memory_order_release);

            return;
        }
    }
}
