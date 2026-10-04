#pragma once

#include "Trace.h"
#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <string_view>
#include <vector>

namespace diagnostics {

struct RecordedEvent {
    uint64_t sequence = 0;
    uint64_t microseconds = 0;
    Event event;
};

struct TraceSnapshot {
    std::vector<ThreadSnapshot> threads;
    std::vector<RecordedEvent> events;
    uint64_t evicted = 0;
    uint64_t lost = 0;
};

// bounded trace storage that may be consumed by a tui, gui or file exporter
class TraceStore final : public TraceSink {
public:
    explicit TraceStore(size_t capacity = 4096);
    void Emit(const Event& event) noexcept override;
    bool ExecutionEnabled() const noexcept override;
    
    // enable or disable frequent dispatcher and hle boundary events
    void SetExecutionEnabled(bool enabled);
    
    // copy consistent thread metadata and event history without reading live guest contexts
    TraceSnapshot Read() const;
    
    // collect an emulator log without writing over a frontend display
    void Log(uint32_t level, std::string_view message) noexcept;

private:
    const size_t capacity_;
    const std::chrono::steady_clock::time_point origin_ = std::chrono::steady_clock::now();
    mutable std::mutex mutex_;
    std::map<uint32_t, ThreadSnapshot> threads_;
    std::deque<RecordedEvent> events_;
    uint64_t sequence_ = 0;
    uint64_t evicted_ = 0;
    std::atomic_uint64_t lost_ = 0;
    std::atomic_bool execution_ = false;
};

}  // namespace diagnostics
