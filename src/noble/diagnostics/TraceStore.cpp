#include "TraceStore.h"
#include <algorithm>

namespace diagnostics {

TraceStore::TraceStore(size_t capacity) : capacity_(std::max<size_t>(1, capacity)) {}

void TraceStore::Emit(const Event& event) noexcept {
    try {
        std::lock_guard lock(mutex_);
        
        if (event.hasSnapshot) {
            auto& thread = threads_[event.thread];
            const auto entry = thread.entry;
            const auto lastBlock = thread.lastBlock;
            thread = event.snapshot;
            
            if (event.kind != EventKind::ThreadCreated)
                thread.entry = entry;
            
                thread.lastBlock = lastBlock;
        }
        
        if (event.kind == EventKind::BlockEntered) {
            const auto thread = threads_.find(event.thread);
            
            if (thread != threads_.end())
                thread->second.lastBlock = event.address;
        }
       
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                                 std::chrono::steady_clock::now() - origin_)
                                 .count();
        events_.push_back({++sequence_, static_cast<uint64_t>(elapsed), event});
        
        if (events_.size() > capacity_) {
            events_.pop_front();
            ++evicted_;
        }
    } catch (...) {
        lost_.fetch_add(1, std::memory_order_relaxed);
    }
}

bool TraceStore::ExecutionEnabled() const noexcept {
    return execution_.load(std::memory_order_relaxed);
}

void TraceStore::SetExecutionEnabled(bool enabled) {
    execution_.store(enabled, std::memory_order_relaxed);
}

TraceSnapshot TraceStore::Read() const {
    std::lock_guard lock(mutex_);

    TraceSnapshot result;
    result.threads.reserve(threads_.size());

    for (const auto& [id, thread] : threads_)
        result.threads.push_back(thread);

    result.events.assign(events_.begin(), events_.end());
    result.evicted = evicted_;
    result.lost = lost_.load(std::memory_order_relaxed);

    return result;
}

void TraceStore::Log(uint32_t level, std::string_view message) noexcept {
    try {
        Event event;
        event.kind = EventKind::Log;
        event.thread = currentWorker.thread;
        event.cpu = currentWorker.cpu;
        event.value = level;
        event.message = message;
        
        Emit(event);
    } catch (...) {
        lost_.fetch_add(1, std::memory_order_relaxed);
    }
}

}  // namespace diagnostics
