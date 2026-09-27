#pragma once

#include <array>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stop_token>
#include <thread>

#include "CpuBackend.h"
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

    explicit Scheduler(Memory& memory);
    ~Scheduler();

    bool Initialise();

    void Start();
    void Stop();

    // append a guest thread to the queue
    void MakeRunnable(KThread* thread);

    KThread* CurrentThread() const;
    void WaitForThread(KThread* thread);
    void TerminateThread(KThread* thread, uint32_t exitCode);

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

    uint64_t quantum_ = 50'000;
    CpuBackend cpu_;
};
