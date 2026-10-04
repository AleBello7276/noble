#pragma once

#include <cstdint>
#include <string>

namespace diagnostics {

struct WorkerContext {
    uint32_t thread = 0;
    uint32_t cpu = UINT32_MAX;
};

inline thread_local WorkerContext currentWorker;

class ScopedWorkerContext {
public:
    ScopedWorkerContext(uint32_t thread, uint32_t cpu) : previous_(currentWorker) {
        currentWorker = {thread, cpu};
    }

    ~ScopedWorkerContext() { currentWorker = previous_; }
    ScopedWorkerContext(const ScopedWorkerContext&) = delete;
    ScopedWorkerContext& operator=(const ScopedWorkerContext&) = delete;

private:
    WorkerContext previous_;
};

enum class EventKind : uint8_t {
    ThreadCreated,
    ThreadReady,
    ThreadScheduled,
    ThreadWaiting,
    ThreadTerminated,
    PriorityChanged,
    BlockEntered,
    CompileStarted,
    CompileFinished,
    HLEEntered,
    HLEReturned,
    Fault,
    Log
};

enum class ThreadStatus : uint8_t { Created, Ready, Running, Waiting, Suspended, Terminated };

struct ThreadSnapshot {
    uint32_t id = 0;
    uint32_t process = 0;
    uint32_t cpu = UINT32_MAX;
    ThreadStatus state = ThreadStatus::Created;
    int32_t priority = 0;
    uint32_t affinity = 0;
    uint32_t entry = 0;
    uint32_t lastBlock = 0;
    uint32_t stackBase = 0;
    uint32_t stackLimit = 0;
    uint32_t tls = 0;
    uint32_t object = 0;
    bool faulted = false;
};

struct Event {
    EventKind kind = EventKind::Log;
    uint32_t thread = 0;
    uint32_t cpu = UINT32_MAX;
    uint32_t address = 0;
    uint32_t value = 0;
    uint32_t auxiliary = 0;
    bool hasSnapshot = false;
    ThreadSnapshot snapshot;
    std::string message;
};

// observe immutable events without depending on a particular frontend or storage format
// the sink must outlive the emulator and contain its own exceptions and synchronization
// callbacks may run under scheduler locks and must not call back into the emulator
class TraceSink {
public:
    virtual ~TraceSink() = default;

    // receive an event from any emulator worker without throwing into guest execution
    virtual void Emit(const Event& event) noexcept = 0;

    // opt into frequent dispatcher events while keeping lifecycle and fault events enabled
    virtual bool ExecutionEnabled() const noexcept { return false; }
};

}  // namespace diagnostics
