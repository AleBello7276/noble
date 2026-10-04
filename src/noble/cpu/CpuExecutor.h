#pragma once
#include "PpcContext.h"
#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <shared_mutex>
#include <stop_token>

#include "JITBackend.h"
#include "diagnostics/Trace.h"

class Memory;

using GuestAddress = uint32_t;

enum class ExecutionReason { TimesliceExpired, Yielded, Waiting, Exited, Fault };

struct ExecutionResult {
    ExecutionReason reason;
    GuestAddress fault_address = 0;
};

class CpuExecutor {
public:
    explicit CpuExecutor(Memory& memory, diagnostics::TraceSink* trace = nullptr);

    // execute from cia and follow nia until termination, shutdown or a guest fault
    ExecutionResult Execute(PPCContext& context, const std::atomic_bool& terminate, std::stop_token stop);

    JITBackend* jit() { return jit_.get(); }

private:
    Memory& memory_;
    diagnostics::TraceSink* trace_;
    std::unique_ptr<JITBackend> jit_;
};
