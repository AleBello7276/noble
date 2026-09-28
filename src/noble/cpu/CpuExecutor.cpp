#include "CpuExecutor.h"

#include "emulator/Memory.h"
#include <mutex>

CpuExecutor::CpuExecutor(Memory& memory) : memory_(memory) {}

ExecutionResult CpuExecutor::Execute(PPCContext& context, const std::atomic_bool& terminate,
                                     std::stop_token stop) {
    if (terminate.load(std::memory_order_acquire) || stop.stop_requested())
        return {ExecutionReason::Exited};
}
