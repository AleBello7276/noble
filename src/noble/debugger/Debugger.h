#pragma once

#include "cpu/PpcContext.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

class Memory;

namespace debugger {

enum class ThreadStatus { Running, Paused, Waiting, Exited, Faulted };
enum class StopReason { None, Pause, Breakpoint, Step, Fault };
enum class StepKind { Into, Over, Out };

struct HistoryEntry {
    uint32_t address;
    uint32_t nextAddress;
};

struct ThreadSnapshot {
    uint32_t id = 0;
    ThreadStatus status = ThreadStatus::Running;
    StopReason reason = StopReason::None;
    uint64_t instructions = 0;
    bool hle = false;
    PPCContext registers{};
    PPCContext previous{};
    std::deque<HistoryEntry> history;
};

struct Instruction {
    uint32_t address;
    uint32_t word = 0;
    bool mapped = false;
    bool breakpoint = false;
    std::string text;
};

// own debugger commands and copied state without depending on a frontend
// attach before execution and keep this object alive until the emulator has stopped
class Debugger {
public:
    explicit Debugger(bool breakOnEntry = true) : pauseNewThreads_(breakOnEntry) {}

    // pause at the next guest instruction boundary, including newly scheduled threads
    void PauseAll();
    bool Pause(uint32_t thread);
    void ContinueAll();
    bool Continue(uint32_t thread);
    // step over and out also require the stack to return to the captured caller depth
    bool Step(uint32_t thread, StepKind kind = StepKind::Into);
    bool AddBreakpoint(uint32_t address);
    bool RemoveBreakpoint(uint32_t address);
    std::vector<uint32_t> Breakpoints() const;
    std::vector<ThreadSnapshot> Threads() const;
    std::optional<ThreadSnapshot> Thread(uint32_t id) const;
    bool WaitForPause(uint32_t thread, std::chrono::milliseconds timeout);

    // copy readable memory only while all observed guest workers are parked or inactive
    std::optional<std::vector<uint8_t>> ReadMemory(uint32_t address, size_t size) const;
    std::vector<Instruction> Disassemble(uint32_t address, size_t count = 16) const;
    // commands use decimal thread ids and hexadecimal guest addresses
    std::string Command(std::string_view command);

    // executor integration, called only by the worker that owns this context
    bool Checkpoint(PPCContext& context, const std::atomic_bool& terminate, std::stop_token stop,
                    bool hle = false);
    void Executed(const PPCContext& context, uint32_t address);
    void Inactive(const PPCContext& context, ThreadStatus status);
    void BindMemory(Memory* memory);

private:
    struct ThreadState {
        ThreadSnapshot snapshot;
        bool pause = false;
        bool parked = false;
        bool stepping = false;
        StepKind step = StepKind::Into;
        uint64_t stopAfter = 0;
        uint32_t target = 0;
        uint64_t stack = 0;
    };
    static uint32_t ThreadId(const PPCContext& context);
    static void CopyRegisters(ThreadSnapshot& snapshot, const PPCContext& context);
    bool InspectionSafeLocked() const;
    mutable std::mutex mutex_;
    std::condition_variable_any cv_;
    std::map<uint32_t, ThreadState> threads_;
    std::set<uint32_t> breakpoints_;
    bool pauseNewThreads_;
    Memory* memory_ = nullptr;
};

const char* Name(ThreadStatus status);
const char* Name(StopReason reason);

}  // namespace debugger
