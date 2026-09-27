#pragma once

enum class ExecutionReason {
    TimesliceExpired,
    Yielded,
    Waiting,
    Exited,
    Fault,
};

struct ExecutionResult {
    ExecutionReason reason;
};

class CpuBackend {};
