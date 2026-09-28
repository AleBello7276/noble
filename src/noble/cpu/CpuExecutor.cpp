#include "CpuExecutor.h"

#include "emulator/Memory.h"
#include <mutex>

#include "cl/CraneliftJIT.h"

CpuExecutor::CpuExecutor(Memory& memory) : memory_(memory), jit_(std::make_unique<CraneliftJIT>(memory)) {}

ExecutionResult CpuExecutor::Execute(PPCContext& context, const std::atomic_bool& terminate,
                                     std::stop_token stop) {
    //    if (terminate.load(std::memory_order_acquire) || stop.stop_requested())
    //        return {ExecutionReason::Exited};

    return {ExecutionReason::Exited};
}
