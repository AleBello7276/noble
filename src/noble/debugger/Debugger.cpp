#include "Debugger.h"

#include "emulator/Memory.h"
#include "kernel/KThread.h"
#include "powerpc-rs.h"
#include <algorithm>
#include <bit>
#include <charconv>
#include <cstring>
#include <format>
#include <sstream>

namespace debugger {

thread_local ExecutionSession* ExecutionSession::current_ = nullptr;

ExecutionSession::ExecutionSession(Debugger* debugger, PPCContext& context, const std::atomic_bool& terminate,
                                   std::stop_token stop)
    : debugger_(debugger), context_(context), terminate_(terminate), stop_(stop) {
    if (debugger_) {
        previous_ = current_;
        current_ = this;
    }
}
ExecutionSession::~ExecutionSession() {
    if (debugger_)
        current_ = previous_;
}
void ExecutionSession::BeginBlock(uint32_t address) {
    address_ = address;
    prechecked_ = true;
    pending_ = hooked_ = false;
}
void ExecutionSession::EndBlock() {
    if (debugger_ && (pending_ || !hooked_))
        debugger_->Executed(context_, address_);
    pending_ = false;
}
uint32_t ExecutionSession::Instruction(PPCContext* context, uint32_t address) {
    auto* session = current_;
    if (!session || context != &session->context_)
        return 0;
    session->hooked_ = true;
    if (session->pending_) {
        context->NIA = address;
        session->debugger_->Executed(*context, session->address_);
        session->pending_ = false;
    }
    context->CIA = context->NIA = address;
    const bool proceed
        = session->prechecked_ ?
              !session->stop_.stop_requested() && !session->terminate_.load(std::memory_order_acquire) :
              session->debugger_->Checkpoint(*context, session->terminate_, session->stop_);
    session->prechecked_ = false;
    session->address_ = address;
    session->pending_ = proceed;
    return proceed ? 1 : 0;
}

uint32_t Debugger::ThreadId(const PPCContext& context) {
    return context.HostThread ? context.HostThread->id() : 0;
}

void Debugger::CopyRegisters(ThreadSnapshot& snapshot, const PPCContext& context) {
    snapshot.registers = context;
    snapshot.registers.HostThread = nullptr;
}

void Debugger::BindMemory(Memory* memory) {
    std::lock_guard lock(mutex_);
    memory_ = memory;
}

bool Debugger::Checkpoint(PPCContext& context, const std::atomic_bool& terminate, std::stop_token stop,
                          bool hle, bool guestBreakpoint) {
    std::unique_lock lock(mutex_);
    auto [it, inserted] = threads_.try_emplace(ThreadId(context));
    auto& state = it->second;
    auto& snapshot = state.snapshot;
    if (inserted) {
        snapshot.id = it->first;
        state.pause = pauseNewThreads_;
        snapshot.previous = context;
        snapshot.previous.HostThread = nullptr;
    }
    CopyRegisters(snapshot, context);
    snapshot.hle = hle;
    StopReason reason = StopReason::None;
    if (context.Fault != PPCFault::None)
        reason = StopReason::Fault;
    else if (guestBreakpoint)
        reason = StopReason::Breakpoint;
    else if (state.pause)
        reason = StopReason::Pause;
    else if (breakpoints_.contains(context.CIA))
        reason = StopReason::Breakpoint;
    else if (state.stepping && snapshot.instructions >= state.stopAfter
             && (state.step == StepKind::Into
                 || (context.CIA == state.target && context.GPRs[1].u64 >= state.stack)))
        reason = StopReason::Step;

    if (reason != StopReason::None) {
        state.pause = true;
        state.stepping = false;
        snapshot.status = context.Fault == PPCFault::None ? ThreadStatus::Paused : ThreadStatus::Faulted;
        snapshot.reason = reason;
        state.parked = true;

        cv_.notify_all();

        // a guest termination request can arrive without a debugger command
        while (state.pause && !stop.stop_requested() && !terminate.load(std::memory_order_acquire))
            cv_.wait_for(lock, stop, std::chrono::milliseconds(50),
                         [&] { return !state.pause || terminate.load(std::memory_order_acquire); });

        state.parked = false;
    }
    if (stop.stop_requested() || terminate.load(std::memory_order_acquire))
        return false;

    snapshot.status = ThreadStatus::Running;
    snapshot.reason = StopReason::None;
    snapshot.previous = snapshot.registers;

    return true;
}

void Debugger::Executed(const PPCContext& context, uint32_t address) {
    std::lock_guard lock(mutex_);

    auto& snapshot = threads_.at(ThreadId(context)).snapshot;
    ++snapshot.instructions;
    snapshot.history.push_back({address, context.NIA});

    if (snapshot.history.size() > 128)
        snapshot.history.pop_front();

    CopyRegisters(snapshot, context);
}

void Debugger::Inactive(const PPCContext& context, ThreadStatus status) {
    std::lock_guard lock(mutex_);

    auto& snapshot = threads_[ThreadId(context)].snapshot;
    snapshot.id = ThreadId(context);

    CopyRegisters(snapshot, context);
    snapshot.status = status;

    cv_.notify_all();
}

void Debugger::PauseAll() {
    std::lock_guard lock(mutex_);

    pauseNewThreads_ = true;

    for (auto& [id, state] : threads_) {
        state.pause = true;

        if (state.parked && state.snapshot.status == ThreadStatus::Running) {
            state.snapshot.status = ThreadStatus::Paused;
            state.snapshot.reason = StopReason::Pause;
            state.stepping = false;
        }
    }

    cv_.notify_all();
}

bool Debugger::Pause(uint32_t id) {
    std::lock_guard lock(mutex_);

    const auto it = threads_.find(id);
    if (it == threads_.end())
        return false;

    it->second.pause = true;
    if (it->second.parked && it->second.snapshot.status == ThreadStatus::Running) {
        it->second.snapshot.status = ThreadStatus::Paused;
        it->second.snapshot.reason = StopReason::Pause;
        it->second.stepping = false;
    }

    cv_.notify_all();
    return true;
}

void Debugger::ContinueAll() {
    std::lock_guard lock(mutex_);

    pauseNewThreads_ = false;

    for (auto& [id, state] : threads_) {
        state.pause = false;
        state.stepping = false;

        if (state.snapshot.status == ThreadStatus::Paused)
            state.snapshot.status = ThreadStatus::Running;
    }

    cv_.notify_all();
}

bool Debugger::Continue(uint32_t id) {
    std::lock_guard lock(mutex_);

    const auto it = threads_.find(id);
    if (it == threads_.end())
        return false;

    it->second.pause = false;
    it->second.stepping = false;
    if (it->second.snapshot.status == ThreadStatus::Paused)
        it->second.snapshot.status = ThreadStatus::Running;

    cv_.notify_all();
    return true;
}

bool Debugger::Step(uint32_t id, StepKind kind) {
    std::lock_guard lock(mutex_);

    const auto it = threads_.find(id);
    if (it == threads_.end() || it->second.snapshot.status != ThreadStatus::Paused)
        return false;

    auto& state = it->second;
    const auto& registers = state.snapshot.registers;
    if (kind == StepKind::Over) {
        uint32_t word = 0;

        if (!memory_ || !memory_->ReadBytes(registers.CIA, std::as_writable_bytes(std::span{&word, 1})))
            return false;

        const codec::Ins instruction(byte_swap(word));
        const bool call
            = instruction.field_lk()
              && (instruction.op == codec::Opcode::B || instruction.op == codec::Opcode::Bc
                  || instruction.op == codec::Opcode::Bclr || instruction.op == codec::Opcode::Bcctr);

        if (!call || state.snapshot.hle)
            kind = StepKind::Into;
    }

    state.step = kind;
    state.target = kind == StepKind::Out ? uint32_t(registers.SPRs.LR) & ~3u : registers.CIA + 4;
    state.stack = registers.GPRs[1].u64;
    state.stopAfter = state.snapshot.instructions + 1;
    state.pause = false;
    state.stepping = true;
    state.snapshot.status = ThreadStatus::Running;

    cv_.notify_all();
    return true;
}

bool Debugger::AddBreakpoint(uint32_t address) {
    if (address & 3)
        return false;

    std::lock_guard lock(mutex_);

    return breakpoints_.insert(address).second;
}

bool Debugger::RemoveBreakpoint(uint32_t address) {
    std::lock_guard lock(mutex_);

    return breakpoints_.erase(address) != 0;
}

std::vector<uint32_t> Debugger::Breakpoints() const {
    std::lock_guard lock(mutex_);

    return {breakpoints_.begin(), breakpoints_.end()};
}

std::vector<ThreadSnapshot> Debugger::Threads() const {
    std::lock_guard lock(mutex_);

    std::vector<ThreadSnapshot> result;
    for (const auto& [id, state] : threads_)
        result.push_back(state.snapshot);

    return result;
}

std::optional<ThreadSnapshot> Debugger::Thread(uint32_t id) const {
    std::lock_guard lock(mutex_);

    const auto it = threads_.find(id);
    return it == threads_.end() ? std::nullopt : std::optional{it->second.snapshot};
}

bool Debugger::WaitForPause(uint32_t id, std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);

    return cv_.wait_for(lock, timeout, [&] {
        const auto it = threads_.find(id);

        return it != threads_.end()
               && (it->second.snapshot.status == ThreadStatus::Paused
                   || it->second.snapshot.status == ThreadStatus::Faulted);
    });
}

bool Debugger::InspectionSafeLocked() const {
    return !threads_.empty() && std::none_of(threads_.begin(), threads_.end(), [](const auto& pair) {
        return pair.second.snapshot.status == ThreadStatus::Running;
    });
}

std::optional<std::vector<uint8_t>> Debugger::ReadMemory(uint32_t address, size_t size) const {
    std::lock_guard lock(mutex_);

    if (!memory_ || size > 4096 || !InspectionSafeLocked())
        return std::nullopt;

    std::vector<uint8_t> bytes(size);
    if (!memory_->ReadBytes(address, std::as_writable_bytes(std::span{bytes})))
        return std::nullopt;

    return bytes;
}

std::vector<Instruction> Debugger::Disassemble(uint32_t address, size_t count) const {
    std::lock_guard lock(mutex_);

    std::vector<Instruction> result;
    count = (std::min)(count, size_t(256));
    for (size_t i = 0; i < count && uint64_t(address) + i * 4 <= UINT32_MAX; ++i) {
        Instruction row{.address = uint32_t(address + i * 4)};

        row.breakpoint = breakpoints_.contains(row.address);
        row.mapped
            = memory_ && memory_->ReadBytes(row.address, std::as_writable_bytes(std::span{&row.word, 1}));

        if (row.mapped) {
            row.word = byte_swap(row.word);
            row.text = codec::Ins(row.word).simplified().to_string();
        } else {
            row.text = "<unmapped>";
        }

        result.push_back(std::move(row));
    }

    return result;
}

const char* Name(ThreadStatus status) {
    switch (status) {
    case ThreadStatus::Running:
        return "running";
    case ThreadStatus::Paused:
        return "paused";
    case ThreadStatus::Waiting:
        return "waiting";
    case ThreadStatus::Exited:
        return "exited";
    case ThreadStatus::Faulted:
        return "faulted";
    }
    return "unknown";
}

const char* Name(StopReason reason) {
    switch (reason) {
    case StopReason::None:
        return "";
    case StopReason::Pause:
        return "pause";
    case StopReason::Breakpoint:
        return "breakpoint";
    case StopReason::Step:
        return "step";
    case StopReason::Fault:
        return "fault";
    }
    return "unknown";
}

std::string Debugger::Command(std::string_view command) {
    std::istringstream input{std::string(command)};

    std::string verb, argument, extra;
    input >> verb >> argument >> extra;
    auto number = [](std::string_view text, int base) -> std::optional<uint32_t> {
        if (text.starts_with("0x") || text.starts_with("0X")) {
            text.remove_prefix(2);
            base = 16;
        }

        uint32_t value = 0;
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
        return !text.empty() && error == std::errc{} && end == text.data() + text.size() ?
                   std::optional{value} :
                   std::nullopt;
    };

    const auto id = number(argument, 10);
    const auto address = number(argument, 16);
    if (verb == "help" || verb.empty())
        return "threads | pause [id] | continue [id] | step id | next id | finish id\nbreak hex-address | "
               "delete hex-address | breaks\nregs id | fregs id | vregs id [decimal-index] | history "
               "id\ndisasm hex-address [decimal-count] | memory hex-address [decimal-size]\nwait id "
               "[decimal-ms] | quit\n";
    if (verb == "threads") {
        std::string text;
        for (const auto& thread : Threads())
            text += std::format("t{} {:8} cia {:08X} lr {:08X} {} instructions {}{}\n", thread.id,
                                Name(thread.status), thread.registers.CIA, uint32_t(thread.registers.SPRs.LR),
                                Name(thread.reason), thread.instructions, thread.hle ? " hle" : "");
        return text;
    }
    if (verb == "pause" || verb == "continue") {
        if (argument.empty()) {
            if (verb == "pause")
                PauseAll();
            else
                ContinueAll();
            return "ok\n";
        }
        return id && (verb == "pause" ? Pause(*id) : Continue(*id)) ? "ok\n" : "unknown thread\n";
    }
    if (verb == "step" || verb == "next" || verb == "finish")
        return id
                       && Step(*id, verb == "next"   ? StepKind::Over :
                                    verb == "finish" ? StepKind::Out :
                                                       StepKind::Into) ?
                   "ok\n" :
                   "thread must be paused\n";
    if (verb == "wait") {
        const auto timeout = extra.empty() ? std::optional<uint32_t>{5000} : number(extra, 10);
        return id && timeout && WaitForPause(*id, std::chrono::milliseconds{(std::min)(*timeout, 60000u)}) ?
                   "stopped\n" :
                   "timeout or invalid arguments\n";
    }
    if (verb == "break" || verb == "delete")
        return address && (verb == "break" ? AddBreakpoint(*address) : RemoveBreakpoint(*address)) ?
                   "ok\n" :
                   "invalid address or breakpoint already in that state\n";
    if (verb == "breaks") {
        std::string text;
        for (auto pc : Breakpoints())
            text += std::format("{:08X}\n", pc);
        return text;
    }
    if (verb == "disasm") {
        const auto count = extra.empty() ? std::optional<uint32_t>{16} : number(extra, 10);
        if (!address || !count || (*address & 3))
            return "invalid address or count\n";
        std::string text;
        for (const auto& row : Disassemble(*address, *count))
            text += std::format("{} {:08X} {:08X} {}\n", row.breakpoint ? '*' : ' ', row.address, row.word,
                                row.text);
        return text;
    }
    if (verb == "memory") {
        const auto size = extra.empty() ? std::optional<uint32_t>{128} : number(extra, 10);
        if (!address || !size)
            return "invalid address or size\n";
        const auto bytes = ReadMemory(*address, *size);
        if (!bytes)
            return "pause all running threads first, and use a readable range of at most 4096 bytes\n";
        std::string text;
        for (size_t i = 0; i < bytes->size(); i += 16) {
            text += std::format("{:08X}  ", *address + uint32_t(i));
            std::string ascii;
            for (size_t j = 0; j < 16; ++j) {
                if (i + j >= bytes->size()) {
                    text += "   ";
                    continue;
                }
                const auto byte = (*bytes)[i + j];
                text += std::format("{:02X} ", byte);
                ascii += byte >= 32 && byte < 127 ? char(byte) : '.';
            }
            text += " " + ascii + "\n";
        }
        return text;
    }
    const auto thread = id ? Thread(*id) : std::nullopt;
    if (!thread)
        return "unknown command or thread, use help\n";
    if (verb == "history") {
        std::string text;
        for (const auto& entry : thread->history)
            text += std::format("{:08X} -> {:08X}\n", entry.address, entry.nextAddress);
        return text;
    }
    if (verb == "regs") {
        const auto& r = thread->registers;
        std::string text = std::format("cia {:08X} nia {:08X} lr {:016X} ctr {:016X}\nmsr {:016X} xer ca={} "
                                       "ov={} so={} fault={} at {:08X}\n",
                                       r.CIA, r.NIA, r.SPRs.LR, r.SPRs.CTR, r.MSR, r.SPRs.XER.CA,
                                       r.SPRs.XER.OV, r.SPRs.XER.SO, uint32_t(r.Fault), r.FaultAddress);
        for (size_t i = 0; i < 32; ++i)
            text += std::format("{} r{:02} {:016X}{}",
                                r.GPRs[i].u64 != thread->previous.GPRs[i].u64 ? '*' : ' ', i, r.GPRs[i].u64,
                                i % 2 == 1 ? '\n' : ' ');
        text += "cr ";
        for (unsigned field = 0; field < 8; ++field) {
            unsigned value = 0;
            for (unsigned bit = 0; bit < 4; ++bit)
                value = (value << 1) | !!r.ControlRegister.bits[field * 4 + bit];
            text += std::format("{}:{:X} ", field, value);
        }
        return text + "\n";
    }
    if (verb == "fregs") {
        std::string text;
        for (unsigned i = 0; i < 32; ++i) {
            const auto value = std::bit_cast<uint64_t>(thread->registers.FPRs[i]);
            text += std::format("{} f{:02} {:016X} {}\n",
                                value != std::bit_cast<uint64_t>(thread->previous.FPRs[i]) ? '*' : ' ', i,
                                value, thread->registers.FPRs[i].f64);
        }
        return text;
    }
    if (verb == "vregs") {
        const auto index = extra.empty() ? std::optional<uint32_t>{0} : number(extra, 10);
        if (!index || *index >= 128)
            return "vector index must be 0..127\n";
        const auto& value = thread->registers.VRs[*index];
        const bool changed = std::memcmp(&value, &thread->previous.VRs[*index], sizeof(value)) != 0;
        std::string text = std::format("{} v{} guest bytes ", changed ? '*' : ' ', *index);
        for (int i = 15; i >= 0; --i)
            text += std::format("{:02X} ", value.bytes[i]);
        text += "\nf32 lanes ";
        for (int i = 3; i >= 0; --i)
            text += std::format("{} ", value.flt[i]);
        return text + "\n";
    }
    return "unknown command, use help\n";
}

}  // namespace debugger
