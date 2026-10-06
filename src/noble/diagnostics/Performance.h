#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace diagnostics {

enum class Phase {
    ModuleAnalysis,
    FunctionScan,
    FunctionCFG,
    EmitIR,
    Verify,
    Codegen,
    Finalize,
    CompileWait,
    DispatchLookup,
    Execute,
    Count
};

struct PhaseSample {
    uint64_t nanoseconds = 0, calls = 0;
};

struct CompileSample {
    uint32_t address, bytes;
    uint64_t nanoseconds;
};

struct PerformanceSnapshot {
    std::array<PhaseSample, size_t(Phase::Count)> phases{};
    uint64_t indirectTargets = 0;
    std::vector<CompileSample> functions;
    std::string Report() const;
};

// enable only for a measured run before starting guest workers
class Performance {
public:
    static inline std::atomic_bool enabled = false;
    static void Add(Phase phase, uint64_t nanoseconds);
    static void Compiled(uint32_t address, uint32_t bytes, uint64_t nanoseconds);
    static void IndirectTargets(size_t count);
    static PerformanceSnapshot Read();
};

// collect worker wall time without reading the clock when profiling is disabled
class PhaseTimer {
public:
    explicit PhaseTimer(Phase phase)
        : phase_(phase), active_(Performance::enabled.load(std::memory_order_relaxed)) {
        if (active_)
            start_ = std::chrono::steady_clock::now();
    }
    ~PhaseTimer() { Stop(); }
    void Stop() {
        if (!active_)
            return;
        Performance::Add(phase_, std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now() - start_)
                                     .count());
        active_ = false;
    }

private:
    Phase phase_;
    bool active_;
    std::chrono::steady_clock::time_point start_;
};

// retain slow compilation addresses for diagnosis without logging from workers
class CompileTimer {
public:
    CompileTimer(uint32_t address, uint32_t bytes)
        : address_(address), bytes_(bytes), active_(Performance::enabled.load(std::memory_order_relaxed)) {
        if (active_)
            start_ = std::chrono::steady_clock::now();
    }
    ~CompileTimer() {
        if (active_)
            Performance::Compiled(address_, bytes_,
                                  std::chrono::duration_cast<std::chrono::nanoseconds>(
                                      std::chrono::steady_clock::now() - start_)
                                      .count());
    }

private:
    uint32_t address_, bytes_;
    bool active_;
    std::chrono::steady_clock::time_point start_;
};
}  // namespace diagnostics
