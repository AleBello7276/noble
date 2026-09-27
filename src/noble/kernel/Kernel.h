#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "KProcess.h"
#include "KThread.h"

class Memory;
class Scheduler;

struct ProcessCreateInfo {
    GuestAddress image_base = 0;
    GuestAddress entry_point = 0;
};

struct ThreadCreateInfo {
    GuestAddress entry_point = 0;
    GuestAddress parameter = 0;

    uint32_t stack_size = 512 * 1024;

    uint8_t affinity_mask = kAllProcessors;
    int32_t priority = 0;

    bool create_suspended = false;
};

class Kernel {
public:
    Kernel(Memory& memory, Scheduler& scheduler);

    bool Initialize();
    void Shutdown();

    KProcess* CreateGuestProcess(const ProcessCreateInfo& info);

    KThread* CreateThread(KProcess* process, const ThreadCreateInfo& info);
    void StartThread(KThread* thread);

    // create Process Main Thread
    KThread* CreateInitialThread(KProcess* process);

    void ExitThread(KThread* thread, uint32_t exit_code);

    void ExitProcess(KProcess* process, uint32_t exit_code);

    KThread* CurrentThread();
    KProcess* CurrentProcess();

private:
    void InitializeThreadContext(KThread& thread, const ThreadCreateInfo& info);

    bool AllocateThreadStack(KThread& thread, uint32_t size);

private:
    Memory& memory_;
    Scheduler& scheduler_;

    uint32_t next_process_id_ = 1;
    uint32_t next_thread_id_ = 1;

    std::vector<std::unique_ptr<KProcess>> processes_;
};
