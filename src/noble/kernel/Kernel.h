#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include "KProcess.h"
#include "KThread.h"
#include "hle/Shims.h"
#include <mutex>
#include <thread>

namespace hle::krnl {
struct TimeStampBundle;
}

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

    // publish the executable guest loader record and retain its original xex header
    bool SetExecutableModule(const XLoader::IImage& image, std::string_view imagePath = {});

    KThread* CreateThread(KProcess* process, const ThreadCreateInfo& info);
    void StartThread(KThread* thread);

    // create Process Main Thread
    KThread* CreateInitialThread(KProcess* process);

    void ExitThread(KThread* thread, uint32_t exit_code);

    void ExitProcess(KProcess* process, uint32_t exit_code);

    KThread* CurrentThread();
    KProcess* CurrentProcess();

    // expose the import registry whose lifetime covers compiled hle wrappers
    hle::Registry& Imports() { return imports_; }
    // allocate a process tls index and initialize the calling thread value
    uint32_t AllocateTLS(KThread& thread);
    // release a process tls index and clear that slot in every thread
    bool FreeTLS(KThread& thread, uint32_t index);
    // read the calling thread value for an allocated tls index
    uint32_t GetTLSValue(KThread& thread, uint32_t index);
    // update the calling thread value for an allocated tls index
    bool SetTLSValue(KThread& thread, uint32_t index, uint32_t value);

private:
    // refresh the shared timestamp record from the kernel clock origin
    void UpdateTimeStampBundle();

    void InitializeThreadContext(KThread& thread, const ThreadCreateInfo& info);

    bool AllocateThreadStack(KThread& thread, uint32_t size);

private:
    Memory& memory_;
    Scheduler& scheduler_;
    hle::Registry imports_;
    GuestAddress executableModule_ = 0;
    GuestAddress executableHeader_ = 0;
    std::mutex tlsMutex_;

    uint32_t next_process_id_ = 1;
    uint32_t next_thread_id_ = 1;

    std::vector<std::unique_ptr<KProcess>> processes_;

    std::chrono::steady_clock::time_point clockStart_;
    uint64_t systemTimeStart_ = 0;
    hle::krnl::TimeStampBundle* timeStampBundle_ = nullptr;
    // destroy the timer before the guest storage and other kernel members
    std::jthread timeStampTimer_;
};
