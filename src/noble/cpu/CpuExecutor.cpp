#include "CpuExecutor.h"

#include "Logger.h"
#include "debugger/Debugger.h"
#include "diagnostics/TraceEvents.h"
#include "emulator/Memory.h"
#include "kernel/KThread.h"
#include <exception>

#include "cl/CraneliftJIT.h"

CpuExecutor::CpuExecutor(Memory& memory, diagnostics::TraceSink* trace, debugger::Debugger* debugger)
    : memory_(memory), trace_(trace), debugger_(debugger),
      jit_(std::make_unique<CraneliftJIT>(memory, debugger != nullptr)) {
    if (debugger_)
        debugger_->BindMemory(&memory_);
}

CpuExecutor::~CpuExecutor() {
    if (debugger_)
        debugger_->BindMemory(nullptr);
}

ExecutionResult CpuExecutor::Execute(PPCContext& context, const std::atomic_bool& terminate,
                                     std::stop_token stop) {
    const auto finish = [&](ExecutionReason reason, GuestAddress fault = 0) -> ExecutionResult {
        if (debugger_) {
            if (reason == ExecutionReason::Fault)
                debugger_->Checkpoint(context, terminate, stop);
            debugger_->Inactive(context, reason == ExecutionReason::Fault  ? debugger::ThreadStatus::Faulted :
                                         reason == ExecutionReason::Exited ? debugger::ThreadStatus::Exited :
                                         reason == ExecutionReason::Waiting ?
                                                                             debugger::ThreadStatus::Waiting :
                                                                             debugger::ThreadStatus::Running);
        }
        return {reason, fault};
    };

    while (true) {
        if (terminate.load(std::memory_order_acquire) || stop.stop_requested())
            return finish(ExecutionReason::Exited);

        if (context.Fault != PPCFault::None)
            return finish(ExecutionReason::Fault, context.FaultAddress);

        if (context.HostThread
            && context.HostThread->mRescheduleRequested.exchange(false, std::memory_order_acq_rel))
            return finish(ExecutionReason::Yielded);

        // cia holds the entry to execute and nia receives the next dispatcher target
        const GuestAddress address = context.CIA;
        if (context.HostThread && address == KThread::kReturnAddress) {
            auto& thread = *context.HostThread;
            thread.exit_code = thread.mReturnValueIsExitCode ? uint32_t(context.GPRs[3].u64) : 0;
            return finish(ExecutionReason::Exited);
        }

        if ((address & 3) != 0 || !memory_.IsMapped(address, 4)) {
            context.Fault = PPCFault::MemoryAccess;
            context.FaultAddress = address;
            return finish(ExecutionReason::Fault, address);
        }

        if (debugger_
            && !debugger_->Checkpoint(context, terminate, stop,
                                      static_cast<CraneliftJIT*>(jit_.get())->IsImport(address)))
            return finish(ExecutionReason::Exited);

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
                return finish(ExecutionReason::Fault, address);
            }
        }

        // compilation may take long enough for a stop request to arrive
        if (terminate.load(std::memory_order_acquire) || stop.stop_requested())
            return finish(ExecutionReason::Exited);

        if (trace_ && trace_->ExecutionEnabled())
            diagnostics::EmitExecution(trace_, diagnostics::EventKind::BlockEntered, context, address);

        block(&context, memory_.GetMemoryBase());
        if (debugger_)
            debugger_->Executed(context, address);

        // retain the executing entry when a jit block or hle shim reports a fault
        if (context.Fault != PPCFault::None)
            return finish(ExecutionReason::Fault, context.FaultAddress);

        context.CIA = context.NIA;
        if (context.Action == HostAction::Wait) {
            context.Action = HostAction::None;
            return finish(ExecutionReason::Waiting);
        }

        if (context.Action == HostAction::Yield) {
            context.Action = HostAction::None;

            if (context.HostThread)
                context.HostThread->mRescheduleRequested.exchange(false, std::memory_order_acq_rel);
            return finish(ExecutionReason::Yielded);
        }
    }
}
