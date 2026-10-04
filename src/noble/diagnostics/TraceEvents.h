#pragma once

#include "Trace.h"
#include "kernel/KThread.h"

namespace diagnostics {

// copy thread metadata only while holding the scheduler lock or before publishing a new thread
inline void EmitThread(TraceSink* sink, EventKind kind, const KThread& thread, uint32_t entry = 0) {
    if (!sink)
        return;

    Event event;
    event.kind = kind;
    event.thread = thread.id();
    event.cpu = thread.mCurrentProcessor;
    event.hasSnapshot = true;
    auto& snapshot = event.snapshot;
    
    snapshot.id = thread.id();
    snapshot.process = thread.process() ? thread.process()->id() : 0;
    snapshot.cpu = thread.mCurrentProcessor;
    snapshot.state = static_cast<ThreadStatus>(thread.mState);
    snapshot.priority = thread.mPriority;
    snapshot.affinity = thread.mAffinityMask;
    snapshot.entry = entry;
    snapshot.stackBase = thread.mStackBase;
    snapshot.stackLimit = thread.mStackLimit;
    snapshot.tls = thread.tls_address;
    snapshot.object = thread.guest_address();
    snapshot.faulted = thread.faulted;
    
    sink->Emit(event);
}

// publish an execution boundary using fields owned by the current guest worker
inline void EmitExecution(TraceSink* sink, EventKind kind, const PPCContext& cpu, uint32_t address,
                          uint32_t value = 0, uint32_t auxiliary = 0) {
    if (!sink)
        return;
    Event event;
    event.kind = kind;
    event.thread = cpu.HostThread ? cpu.HostThread->id() : 0;
    event.cpu = cpu.HostThread ? cpu.HostThread->mCurrentProcessor : UINT32_MAX;
    event.address = address;
    event.value = value;
    event.auxiliary = auxiliary;
    sink->Emit(event);
}

}  // namespace diagnostics
