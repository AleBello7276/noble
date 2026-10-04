#include "CpuExecutor.h"

#include "Logger.h"
#include "diagnostics/TraceEvents.h"
#include "emulator/Memory.h"
#include "kernel/KThread.h"
#include <exception>

#include "cl/CraneliftJIT.h"

CpuExecutor::CpuExecutor(Memory& memory, diagnostics::TraceSink* trace)
    : memory_(memory), trace_(trace), jit_(std::make_unique<CraneliftJIT>(memory)) {}

ExecutionResult CpuExecutor::Execute(PPCContext& context, const std::atomic_bool& terminate,
                                     std::stop_token stop) {
    while (true) {
        if (terminate.load(std::memory_order_acquire) || stop.stop_requested())
            return {ExecutionReason::Exited};

        if (context.Fault != PPCFault::None)
            return {ExecutionReason::Fault, context.FaultAddress};

        // cia holds the entry to execute and nia receives the next dispatcher target
        const GuestAddress address = context.CIA;
        if (context.HostThread && address == KThread::kReturnAddress) {
            auto& thread = *context.HostThread;
            thread.exit_code = thread.mReturnValueIsExitCode ? uint32_t(context.GPRs[3].u64) : 0;
            return {ExecutionReason::Exited};
        }

        if ((address & 3) != 0 || !memory_.IsMapped(address, 4)) {
            context.Fault = PPCFault::MemoryAccess;
            context.FaultAddress = address;
            return {ExecutionReason::Fault, address};
        }

        JITBlock block = jit_->FindBlock(address);
        if (!block) {
            diagnostics::EmitExecution(trace_, diagnostics::EventKind::CompileStarted, context, address);

            try {
                jit_->CompileJITBlock(address);
                block = jit_->FindBlock(address);
            } catch (const std::exception& error) {
                LOG_ERROR("JIT compilation failed at {:08X}: {}\n", address, error.what());
            }

            diagnostics::EmitExecution(trace_, diagnostics::EventKind::CompileFinished, context, address,
                                       block ? 1 : 0);

            if (!block) {
                context.Fault = PPCFault::UncompiledTarget;
                context.FaultAddress = address;
                return {ExecutionReason::Fault, address};
            }
        }

        // compilation may take long enough for a stop request to arrive
        if (terminate.load(std::memory_order_acquire) || stop.stop_requested())
            return {ExecutionReason::Exited};

        if (trace_ && trace_->ExecutionEnabled())
            diagnostics::EmitExecution(trace_, diagnostics::EventKind::BlockEntered, context, address);

        block(&context, memory_.GetMemoryBase());

        // retain the executing entry when a jit block or hle shim reports a fault
        if (context.Fault != PPCFault::None)
            return {ExecutionReason::Fault, context.FaultAddress};

        context.CIA = context.NIA;
        if (context.Action == HostAction::Wait) {
            context.Action = HostAction::None;
            return {ExecutionReason::Waiting};
        }
    }
}
