#include "Kernel.h"

#include <atomic>
#include <limits>

constexpr uint32_t statusSuccess = 0;
constexpr uint32_t statusTimeout = 0x102;
constexpr uint32_t statusInvalidParameter = 0xC000000D;
constexpr uint32_t statusAccessViolation = 0xC0000005;
constexpr uint32_t statusNotSupported = 0xC00000BB;
constexpr uint32_t statusThreadTerminating = 0xC000004B;

X_DISPATCH_HEADER* Header(Memory& memory, GuestAddress address) {
    if (address % 4 || !memory.IsAccessible(address, sizeof(X_DISPATCH_HEADER), true))
        return nullptr;

    return static_cast<X_DISPATCH_HEADER*>(memory.Translate(address, sizeof(X_DISPATCH_HEADER)));
}

bool Supported(X_OBJECT_TYPES type) {
    return type == EventNotificationObject || type == EventSynchronizationObject || type == SemaphoreObject
           || type == ThreadObject || type == TimerNotificationObject || type == TimerSynchronizationObject;
}

// acquire synchronization events and semaphore tokens without consuming notification signals
bool Acquire(X_DISPATCH_HEADER& header) {
    std::atomic_ref signal(header.signal_state);
    auto current = signal.load(std::memory_order_acquire);

    while (uint32_t(current) && uint32_t(current) <= INT32_MAX) {
        if (header.type == EventNotificationObject || header.type == TimerNotificationObject)
            return true;

        const be<uint32_t> desired(header.type == SemaphoreObject ? uint32_t(current) - 1 : 0);
        if (signal.compare_exchange_weak(current, desired, std::memory_order_acq_rel))
            return true;
    }

    return false;
}

uint64_t Kernel::WaitClockTicks() const {
    using Units = std::chrono::duration<uint64_t, std::ratio<1, 10000000>>;
    return std::chrono::duration_cast<Units>(std::chrono::steady_clock::now() - clockStart_).count();
}

uint32_t Kernel::WaitForSingleObject(KThread& thread, PPCContext& cpu, GuestAddress object, uint32_t reason,
                                     uint32_t mode, bool alertable, std::optional<int64_t> timeout) {
    if (mode > 1 || reason > UINT8_MAX)
        return statusInvalidParameter;

    std::scoped_lock lock(dispatcherMutex_);
    auto* header = Header(memory_, object);
    if (!header)
        return statusAccessViolation;

    if (!Supported(header->type))
        return statusNotSupported;

    if (header->type == ThreadObject) {
        auto* target = dynamic_cast<KThread*>(LookupGuestObject(object));

        if (!target)
            return statusInvalidParameter;

        if (scheduler_.IsThreadTerminated(target))
            return statusSuccess;

    } else if (Acquire(*header)) {
        return statusSuccess;
    }

    DispatcherWait wait{&thread, object, {}, false};
    if (timeout) {
        const uint64_t now = WaitClockTicks();

        if (*timeout >= 0) {
            wait.absolute = true;
            wait.deadline = uint64_t(*timeout);

            if (*wait.deadline <= systemTimeStart_ + now)
                return statusTimeout;

        } else {
            // unsigned subtraction also handles the most negative signed interval
            const uint64_t interval = uint64_t(0) - uint64_t(*timeout);
            wait.deadline = interval > UINT64_MAX - now ? UINT64_MAX : now + interval;
        }
    }

    dispatcherWaits_.push_back(wait);
    if (!scheduler_.PrepareWait(&thread)) {
        dispatcherWaits_.pop_back();
        return statusThreadTerminating;
    }

    if (thread.guestThread_) {
        thread.guestThread_->waitReason = static_cast<uint8_t>(reason);
        thread.guestThread_->processorMode = static_cast<uint8_t>(mode);
        thread.guestThread_->alertable = alertable;
    }
    cpu.Action = HostAction::Wait;
    return statusSuccess;
}

void Kernel::InitializeEvent(GuestAddress address, uint32_t type, bool state) {
    if (type > 1)
        throw std::invalid_argument("invalid event type");

    std::scoped_lock lock(dispatcherMutex_);
    auto* header = Header(memory_, address);
    if (!header)
        throw std::out_of_range("invalid guest event address");

    if (std::any_of(dispatcherWaits_.begin(), dispatcherWaits_.end(),
                    [address](const auto& wait) { return wait.object == address; }))
        throw std::logic_error("cannot reinitialize an event with pending waits");

    *header = {};
    header->type = static_cast<X_OBJECT_TYPES>(type);
    header->size = sizeof(X_KEVENT) / 4;
    header->signal_state = state ? 1 : 0;
    const GuestAddress list = address + offsetof(X_DISPATCH_HEADER, wait_list);
    header->wait_list.Flink = header->wait_list.Blink = list;
}

uint32_t Kernel::SetEvent(GuestAddress address) {
    std::scoped_lock lock(dispatcherMutex_);

    auto* header = Header(memory_, address);

    if (!header || (header->type != EventNotificationObject && header->type != EventSynchronizationObject))
        throw std::invalid_argument("invalid guest event");

    const uint32_t previous = std::atomic_ref(header->signal_state).exchange(be<uint32_t>(1));
    PollDispatcherWaitsLocked();

    return previous;
}

uint32_t Kernel::ResetEvent(GuestAddress address) {
    std::scoped_lock lock(dispatcherMutex_);

    auto* header = Header(memory_, address);
    if (!header || (header->type != EventNotificationObject && header->type != EventSynchronizationObject))
        throw std::invalid_argument("invalid guest event");

    return uint32_t(std::atomic_ref(header->signal_state).exchange(be<uint32_t>(0)));
}

void Kernel::PollDispatcherWaits() {
    std::scoped_lock lock(dispatcherMutex_);
    PollDispatcherWaitsLocked();
}

void Kernel::PollDispatcherWaitsLocked() {
    const uint64_t relativeNow = WaitClockTicks();
    const uint64_t absoluteNow = systemTimeStart_ + relativeNow;

    for (auto it = dispatcherWaits_.begin(); it != dispatcherWaits_.end();) {
        auto& wait = *it;

        if (wait.thread->mTerminateRequested.load(std::memory_order_acquire)
            || scheduler_.IsThreadTerminated(wait.thread)) {
            it = dispatcherWaits_.erase(it);
            continue;
        }

        auto* header = Header(memory_, wait.object);
        bool completed = false;

        if (!header) {
            completed = scheduler_.WakeThread(wait.thread, statusAccessViolation);
        } else if (!Supported(header->type)) {
            completed = scheduler_.WakeThread(wait.thread, statusNotSupported);
        } else if (header->type == ThreadObject) {
            auto* target = dynamic_cast<KThread*>(LookupGuestObject(wait.object));
            if (!target)
                completed = scheduler_.WakeThread(wait.thread, statusInvalidParameter);

            else if (scheduler_.IsThreadTerminated(target))
                completed = scheduler_.WakeThread(wait.thread, statusSuccess);
        } else {
            // only consume the signal if the waiter can still be resumed
            completed
                = scheduler_.WakeThread(wait.thread, statusSuccess, [header] { return Acquire(*header); });
        }

        if (!completed && wait.deadline && *wait.deadline <= (wait.absolute ? absoluteNow : relativeNow))
            completed = scheduler_.WakeThread(wait.thread, statusTimeout);

        if (completed)
            it = dispatcherWaits_.erase(it);
        else
            ++it;
    }
}
