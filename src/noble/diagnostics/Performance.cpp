#include "Performance.h"
#include <algorithm>
#include <format>
#include <mutex>

namespace diagnostics {

struct Counter {
    std::atomic_uint64_t nanoseconds{0}, calls{0};
};

std::array<Counter, size_t(Phase::Count)> counters;
std::atomic_uint64_t indirectTargets{0};
std::mutex compileMutex;
std::vector<CompileSample> compiled;

void Performance::Add(Phase phase, uint64_t nanoseconds) {
    counters[size_t(phase)].nanoseconds.fetch_add(nanoseconds, std::memory_order_relaxed);
    counters[size_t(phase)].calls.fetch_add(1, std::memory_order_relaxed);
}

void Performance::Compiled(uint32_t address, uint32_t bytes, uint64_t nanoseconds) {
    std::lock_guard lock(compileMutex);
    compiled.push_back({address, bytes, nanoseconds});
}

void Performance::IndirectTargets(size_t count) {
    if (enabled.load(std::memory_order_relaxed))
        indirectTargets.fetch_add(count, std::memory_order_relaxed);
}

PerformanceSnapshot Performance::Read() {
    PerformanceSnapshot result;

    for (size_t i = 0; i < counters.size(); ++i)
        result.phases[i] = {counters[i].nanoseconds.load(std::memory_order_relaxed),
                            counters[i].calls.load(std::memory_order_relaxed)};

    result.indirectTargets = indirectTargets.load(std::memory_order_relaxed);

    std::lock_guard lock(compileMutex);

    result.functions = compiled;
    return result;
}

std::string PerformanceSnapshot::Report() const {
    constexpr std::array names{"module analysis", "function scan",  "function cfg", "emit ir",
                               "verify",          "codegen",        "finalize",     "compile lock wait",
                               "dispatch lookup", "guest execution"};
    std::string result = "worker wall times overlap across threads and nested phases\nphase                    worker ms      calls\n";
    for (size_t i = 0; i < phases.size(); ++i)
        result += std::format("{:23} {:10.3f} {:10}\n", names[i], double(phases[i].nanoseconds) / 1e6,
                              phases[i].calls);
    result += std::format("indirect target comparisons emitted: {}\n", indirectTargets);

    auto slow = functions;
    std::sort(slow.begin(), slow.end(),
              [](const auto& a, const auto& b) { return a.nanoseconds > b.nanoseconds; });

    result += "slowest compilations (inclusive)\n";
    for (size_t i = 0; i < std::min(size_t(10), slow.size()); ++i)
        result += std::format("{:08X} {:6} guest bytes {:10.3f} ms\n", slow[i].address, slow[i].bytes,
                              double(slow[i].nanoseconds) / 1e6);

    return result;
}

}  // namespace diagnostics
