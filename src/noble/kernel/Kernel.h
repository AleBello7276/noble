#pragma once

/*
    many structs found here comes from Xenia emulator
*/

#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <unordered_map>
#include <vector>

#include "KernelTypes.h"
#include "cpu/Scheduler.h"
#include "gpu/GPUBackend.h"
#include "hle/Shims.h"
#include <mutex>
#include <thread>

class Kernel {
public:
    Kernel(Memory& memory, Scheduler& scheduler, std::unique_ptr<GPUBackend> gpu = {});

    bool Initialize();
    void Shutdown();

    KProcess* CreateGuestProcess(const ProcessCreateInfo& info);

    // find the title process or create the system process on its first thread request
    KProcess* GetThreadProcess(bool system);

    KernelObject* LookupGuestObject(GuestAddress address);

    // publish the executable guest loader record and retain its original xex header
    bool SetExecutableModule(const XLoader::IImage& image, std::string_view imagePath = {});

    // test a system flag bit from the executable's retained xex metadata
    bool CheckExecutablePrivilege(uint32_t privilege) const {
        return executableModule_ && privilege < 32 && (executableSystemFlags_ & (uint32_t(1) << privilege));
    }

    KThread* CreateThread(KProcess* process, const ThreadCreateInfo& info);
    void StartThread(KThread* thread);

    // update a guest thread base priority through the scheduler lock
    int32_t SetBasePriorityThread(KThread* thread, int32_t increment);

    // create Process Main Thread
    KThread* CreateInitialThread(KProcess* process);

    void ExitThread(KThread* thread, uint32_t exit_code);

    void ExitProcess(KProcess* process, uint32_t exit_code);

    KThread* CurrentThread();
    KProcess* CurrentProcess();

    struct TitleTerminateNotification {
        GuestAddress routine;
        uint32_t priority;
    };

    // retain a guest callback and priority for future title termination delivery
    void RegisterTitleTerminateNotification(GuestAddress routine, uint32_t priority);
    // remove the first registration for this guest callback if present
    void RemoveTitleTerminateNotification(GuestAddress routine);
    // copy registrations in insertion order without retaining the notification mutex
    std::vector<TitleTerminateNotification> GetTitleTerminateNotifications() const;

    using GraphicsInterruptCallback = GPUBackend::InterruptCallback;

    // expose the graphics backend owned by this kernel with null graphics as the default
    GPUBackend& GPU() { return *gpu_; }
    const GPUBackend& GPU() const { return *gpu_; }

    // replace the graphics interrupt callback and its opaque guest argument as one registration
    // a zero routine disables callback delivery
    void SetGraphicsInterruptCallback(GuestAddress routine, GuestAddress userData);
    // copy the callback and argument together for future guest interrupt delivery
    GraphicsInterruptCallback GetGraphicsInterruptCallback() const;

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

    // acquire a recursive guest critical section or queue the thread for a scheduler wait
    bool EnterCriticalSection(KThread& thread, GuestAddress address, bool tryOnly = false);
    // acquire a section whose guest pointer has already been validated by a shim
    bool EnterCriticalSection(KThread& thread, hle::Pointer<hle::krnl::CriticalSection> section,
                              bool tryOnly = false);
    // release one recursion level and hand ownership to the next live waiter
    void LeaveCriticalSection(KThread& thread, GuestAddress address);
    // release a section whose guest pointer has already been validated by a shim
    void LeaveCriticalSection(KThread& thread, hle::Pointer<hle::krnl::CriticalSection> section);

private:
    KProcess* CreateGuestProcessLocked(const ProcessCreateInfo& info);
    // refresh the shared timestamp record from the kernel clock origin
    void UpdateTimeStampBundle();

    void InitializeThreadContext(KThread& thread, const ThreadCreateInfo& info);

    bool AllocateThreadStack(KThread& thread, uint32_t size);

    // allocate and initialize the guest kthread, kpcr and tls storage
    bool InitializeGuestThread(KThread& thread, const ThreadCreateInfo& info);
    // release all guest allocations retained by a host thread
    void FreeGuestThread(KThread& thread);

private:
    Memory& memory_;
    Scheduler& scheduler_;
    std::unique_ptr<GPUBackend> gpu_;
    hle::Registry imports_;
    GuestAddress executableModule_ = 0;
    GuestAddress executableHeader_ = 0;
    uint32_t executableSystemFlags_ = 0;
    uint32_t executableTLSSlots_ = KProcess::TLS_SLOT_COUNT;
    uint32_t executableTLSSize_ = 0;
    uint32_t executableTLSRawSize_ = 0;
    GuestAddress executableTLSTemplate_ = 0;
    std::mutex threadObjectsMutex_;
    std::mutex tlsMutex_;
    std::mutex criticalSectionMutex_;
    mutable std::mutex titleTerminateMutex_;
    std::vector<TitleTerminateNotification> titleTerminateNotifications_;
    std::unordered_map<GuestAddress, std::deque<KThread*>> criticalSectionWaiters_;

    uint32_t next_process_id_ = 1;
    uint32_t next_thread_id_ = 1;

    std::vector<std::unique_ptr<KProcess>> processes_;

    std::chrono::steady_clock::time_point clockStart_;
    uint64_t systemTimeStart_ = 0;
    hle::krnl::TimeStampBundle* timeStampBundle_ = nullptr;
    // destroy the timer before the guest storage and other kernel members
    std::jthread timeStampTimer_;
};
