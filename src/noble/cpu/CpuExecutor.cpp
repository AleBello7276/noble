#include "CpuExecutor.h"

#include "Logger.h"
#include "emulator/Memory.h"
#include <exception>

#include "cl/CraneliftJIT.h"

CpuExecutor::CpuExecutor(Memory& memory) : memory_(memory), jit_(std::make_unique<CraneliftJIT>(memory)) {}

ExecutionResult CpuExecutor::Execute(PPCContext& context, const std::atomic_bool& terminate,
                                     std::stop_token stop) {
    while (true) {
        if (terminate.load(std::memory_order_acquire) || stop.stop_requested())
            return {ExecutionReason::Exited};

        if (context.Fault != PPCFault::None)
            return {ExecutionReason::Fault, context.FaultAddress};

        // cia holds the entry to execute and nia receives the next dispatcher target
        const GuestAddress address = context.CIA;
        if ((address & 3) != 0 || !memory_.IsMapped(address, 4)) {
            context.Fault = PPCFault::MemoryAccess;
            context.FaultAddress = address;
            return {ExecutionReason::Fault, address};
        }

        JITBlock block = jit_->FindBlock(address);
        if (!block) {
            try {
                jit_->CompileJITBlock(address);
                block = jit_->FindBlock(address);
            } catch (const std::exception& error) {
                LOG_ERROR("JIT compilation failed at {:08X}: {}\n", address, error.what());
            }

            if (!block) {
                context.Fault = PPCFault::UncompiledTarget;
                context.FaultAddress = address;
                return {ExecutionReason::Fault, address};
            }
        }

        // compilation may take long enough for a stop request to arrive
        if (terminate.load(std::memory_order_acquire) || stop.stop_requested())
            return {ExecutionReason::Exited};

        block(&context, memory_.GetMemoryBase());

        // retain the executing entry when a jit block or hle shim reports a fault
        if (context.Fault != PPCFault::None)
            return {ExecutionReason::Fault, context.FaultAddress};

        context.CIA = context.NIA;
    }
}
