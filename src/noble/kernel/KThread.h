#pragma once
#include <array>
#include <atomic>
#include <stdint.h>

#include "KObject.h"
#include "KProcess.h"
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

constexpr uint32_t kInvalidProcessor = UINT32_MAX;
constexpr uint8_t kAllProcessors = 0x3F;

using HWT_ID = uint32_t;
using ThreadID = uint32_t;
using ThreadAffinity = uint8_t;
using ThreadPriority = int32_t;

class KThread final : public KernelObject {
public:
    KThread(ThreadID id, KProcess* process)
        : KernelObject(KernelObjectType::KThread), id_(id), process_(process) {}

    uint32_t id() const { return id_; }

    KProcess* process() const { return process_; }

public:
    PPCContext mContext{};
    GuestAddress mStackBase = 0;
    GuestAddress mStackLimit = 0;
    GuestAddress tls_address = 0;
    std::array<uint32_t, KProcess::TLS_SLOT_COUNT> mTlsValues{};

public:
    ThreadState mState = ThreadState::Created;

    ThreadAffinity mAffinityMask = kAllProcessors;
    ThreadPriority mPriority = 0;

    HWT_ID mCurrentProcessor = kInvalidProcessor;
    HWT_ID mLastProcessor = kInvalidProcessor;

    uint32_t suspend_count = 0;
    uint32_t exit_code = 0;
    bool faulted = false;
    std::atomic_bool mTerminateRequested = false;
    // scheduler mutex protects a wait prepared while the worker is still returning from guest code
    bool mWaitPending = false;

private:
    ThreadID id_;
    KProcess* process_;
};
