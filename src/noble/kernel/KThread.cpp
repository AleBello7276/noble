#include "KThread.h"

#include "core/byte_swap.h"
#include <chrono>
#include <stdexcept>

void KThread::EnterCriticalRegion() {
    if (!guestThread_)
        throw std::runtime_error("guest thread has no kthread record");
    --guestThread_->apcDisableCount;
}

void KThread::LeaveCriticalRegion() {
    if (!guestThread_)
        throw std::runtime_error("guest thread has no kthread record");
    ++guestThread_->apcDisableCount;
}

void KThread::EnableFpuExceptions(bool enabled) {
    if (!guestThread_)
        throw std::runtime_error("guest thread has no kthread object");

    // guest floating point exception delivery is not implemented yet
    guestThread_->fpuExceptions = enabled;
}

void KThread::SyncGuestState() {
    if (!guestThread_)
        return;

    if (mState == ThreadState::Running && guestThread_->state != 2)
        guestThread_->contextSwitches = byte_swap(byte_swap(guestThread_->contextSwitches) + 1);

    switch (mState) {
    case ThreadState::Created:
    case ThreadState::Suspended:
        guestThread_->state = 0;
        break;
    case ThreadState::Ready:
        guestThread_->state = 1;
        break;
    case ThreadState::Running:
        guestThread_->state = 2;
        break;
    case ThreadState::Waiting:
        guestThread_->state = 5;
        break;
    case ThreadState::Terminated:
        guestThread_->state = 4;
        break;
    }

    guestThread_->suspendCount = static_cast<uint8_t>(suspend_count);
    guestThread_->terminated = mState == ThreadState::Terminated;
    if (mCurrentProcessor != kInvalidProcessor) {
        guestThread_->currentCPU = static_cast<uint8_t>(mCurrentProcessor);
        guestPCR_->prcbData.currentCPU = static_cast<uint8_t>(mCurrentProcessor);
        guestPCR_->prcbData.processorMask = byte_swap(uint32_t(1) << mCurrentProcessor);
    }

    if (mState == ThreadState::Terminated) {
        process_->DetachGuestThread(*this);
        guestThread_->header.signalState = byte_swap(uint32_t(1));
        guestThread_->exitStatus = byte_swap(exit_code);
        if (!guestThread_->exitTime) {
            using Units = std::chrono::duration<uint64_t, std::ratio<1, 10000000>>;
            const auto now = std::chrono::system_clock::now().time_since_epoch();
            guestThread_->exitTime
                = byte_swap(std::chrono::duration_cast<Units>(now).count() + uint64_t(116444736000000000));
        }
    }
}
