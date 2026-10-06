#include "Logger.h"
#include "TraceView.h"
#include "DebuggerView.h"
#include "debugger/Debugger.h"
#include "emulator/Emulator.h"
#include <atomic>
#include <iostream>
#include <thread>

namespace {

// restore normal logging after workers stop and before trace storage is destroyed
class TraceLogging {
public:
    explicit TraceLogging(diagnostics::TraceStore& store) {
        Logger::SetSink(
            [](LogLevel level, std::string_view message, void* context) noexcept {
                static_cast<diagnostics::TraceStore*>(context)->Log(static_cast<uint32_t>(level), message);
            },
            &store);
    }
    ~TraceLogging() { Logger::SetSink(nullptr); }
};

}  // namespace

int main(int argc, char* argv[]) {
    bool debug = false;
    bool trace = false;
    bool valid = argc >= 2;
    for (int i = 2; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--debug") debug = true;
        else if (std::string_view(argv[i]) == "--trace-execution") trace = true;
        else valid = false;
    }
    if (!valid) {
        std::cerr << "usage: noble-tui <title.xex> [--debug] [--trace-execution]\n";
        return 1;
    }

    diagnostics::TraceStore store;
    store.SetExecutionEnabled(trace);
    TraceLogging logging(store);
    debugger::Debugger debugger;
    Emulator emulator(&store, debug ? &debugger : nullptr);

    if (!emulator.Initialise() || !emulator.LoadTitle(argv[1])) {
        for (const auto& recorded : store.Read().events)
            if (recorded.event.kind == diagnostics::EventKind::Log)
                std::cerr << recorded.event.message << '\n';

        return 1;
    }

    std::atomic_int result = -1;
    std::jthread execution([&] {
        try {
            result.store(emulator.Run() ? 0 : 1, std::memory_order_release);
        } catch (const std::exception& error) {
            store.Log(static_cast<uint32_t>(LogLevel::Error), error.what());
            result.store(1, std::memory_order_release);
        }
    });

    bool frontendFailed = false;
    try {
        if (debug)
            tui::RunDebuggerView(debugger, store, [&] { emulator.RequestStop(); });
        else
            tui::RunTraceView(store, result, [&] { emulator.RequestStop(); });
    } catch (const std::exception& error) {
        store.Log(static_cast<uint32_t>(LogLevel::Error), error.what());
        frontendFailed = true;
    }

    emulator.RequestStop();
    execution.join();
    emulator.Shutdown();

    return frontendFailed ? 1 : result.load(std::memory_order_acquire);
}
