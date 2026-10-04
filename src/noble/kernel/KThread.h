#pragma once
#include <atomic>
#include <optional>
#include <stdint.h>

#include "KObject.h"
#include "KProcess.h"
#include "KernelTypes.h"
#include "core/endian.h"
#include "cpu/PpcContext.h"

enum class ThreadState : uint8_t {
    Created,
    Ready,
    Running,
    Waiting,
    Suspended,
    Terminated,
};

class KProcess;

using GuestAddress = uint32_t;

using HWT_ID = uint32_t;
using ThreadID = uint32_t;
using ThreadAffinity = uint8_t;
using ThreadPriority = int32_t;

// https://github.com/xenia-canary/xenia-canary/blob/canary_experimental/src/xenia/kernel/xthread.h#L66
struct XDPC {
    be<uint16_t> type;
    uint8_t selected_cpu_number;
    uint8_t desired_cpu_number;
    X_LIST_ENTRY list_entry;
    be<uint32_t> routine;
    be<uint32_t> context;
    be<uint32_t> arg1;
    be<uint32_t> arg2;

    void Initialize(uint32_t guest_func, uint32_t guest_context) {
        type = 19;
        selected_cpu_number = 0;
        desired_cpu_number = 0;
        routine = guest_func;
        context = guest_context;
    }
};

class KThread final : public KernelObject {
public:
    // dispatcher recognizes a return to this unmapped address as thread completion
    static constexpr GuestAddress kReturnAddress = 0xFFFFFFFC;
    KThread(ThreadID id, KProcess* process)
        : KernelObject(KernelObjectType::KThread), id_(id), process_(process) {}

    uint32_t id() const { return id_; }

    // get the handle allocated in the creating process
    Handle handle() const { return handle_; }

    KProcess* process() const { return process_; }

    // get the guest kthread pointer used by kernel objects and critical section owners
    GuestAddress guest_address() const override { return guestAddress_; }

    // get the per thread kpcr address held in guest r13
    GuestAddress pcr_address() const { return pcrAddress_; }

    // publish scheduler state while holding the scheduler mutex
    void SyncGuestState();

public:
    PPCContext mContext{};
    GuestAddress mStackBase = 0;
    GuestAddress mStackLimit = 0;
    GuestAddress tls_address = 0;

public:
    ThreadState mState = ThreadState::Created;

    ThreadAffinity mAffinityMask = kAllProcessors;
    ThreadPriority mPriority = 0;
    ThreadPriority mBasePriority = 0;

    HWT_ID mCurrentProcessor = kInvalidProcessor;
    HWT_ID mLastProcessor = kInvalidProcessor;

    uint32_t suspend_count = 0;
    uint32_t exit_code = 0;
    bool faulted = false;
    bool mReturnValueIsExitCode = true;
    std::atomic_bool mTerminateRequested = false;
    // scheduler mutex protects a wait prepared while the worker is still returning from guest code
    bool mWaitPending = false;
    // retain an early completion until the executing worker has returned from the shim
    std::optional<uint32_t> mWaitResult;

private:
    friend class Kernel;
    friend class KProcess;
    friend class Scheduler;
    ThreadID id_;
    Handle handle_ = 0;
    KProcess* handleProcess_ = nullptr;
    KProcess* process_;
    GuestAddress guestAddress_ = 0;
    GuestAddress pcrAddress_ = 0;
    GuestAddress tlsAllocation_ = 0;
    GuestKernelThread* guestThread_ = nullptr;
    GuestProcessorRegion* guestPCR_ = nullptr;
    bool guestProcessEntryLinked_ = false;
};
