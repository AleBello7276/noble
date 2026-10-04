#pragma once

#include <array>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stop_token>
#include <thread>

#include "CpuExecutor.h"
#include "kernel/KThread.h"

struct HardwareThread {
    HWT_ID id = 0;

    KThread* current_thread = nullptr;
};

/* Emulator Sheduler */
/* Schedules the 6 hardware threads */
class Scheduler {
public:
    static constexpr size_t kProcessorCount = 6;
    static constexpr ThreadAffinity kAllProcessorsMask = 0x3F;

    explicit Scheduler(CpuExecutor& cpu, diagnostics::TraceSink* trace = nullptr);
    ~Scheduler();

    bool Initialise();

    void Start();
    void Stop();

    // append a guest thread to the queue
    void MakeRunnable(KThread* thread);

    KThread* CurrentThread() const;
    void WaitForThread(KThread* thread);
    void TerminateThread(KThread* thread, uint32_t exitCode);

    // set the base priority relative to the process and return the previous increment
    int32_t SetBasePriorityThread(KThread* thread, int32_t increment);

    // prepare a guest wait before its current worker returns to the scheduler
    bool PrepareWait(KThread* thread);

    // wake a prepared or parked wait without scheduling a thread on two workers
    bool WakeThread(KThread* thread);

private:
    /* beating main loop */
    void WorkerMain(HWT_ID processor_id, std::stop_token stop_token);

    KThread* PickNextThreadLocked(HWT_ID processor_id);

    bool HasRunnableThreadLocked(HWT_ID processor_id) const;

    void HandleExecutionResult(HWT_ID processor_id, KThread* thread, ExecutionResult result);

    std::array<HardwareThread, kProcessorCount> processors_;
    std::array<std::jthread, kProcessorCount> workers_;
    std::deque<KThread*> ready_queue_;

    std::mutex mutex_;
    std::mutex lifecycle_mutex_;
    std::condition_variable_any cv_;

    bool started_ = false;

    CpuExecutor& cpu_;
    diagnostics::TraceSink* trace_;
};
