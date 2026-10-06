#include "DebuggerView.h"
#include "Logger.h"
#include "TraceView.h"
#include "config/Config.h"
#include "debugger/Debugger.h"
#include "diagnostics/Performance.h"
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

int main(int argc, char* argv[]) try {
    const auto launch = config::ParseLaunch(argc, argv, config::Frontend::TUI);
    if (launch.help) {
        std::cout << config::Help(argv[0]);
        return 0;
    }
    if (launch.printConfig) {
        std::cout << launch.config.Describe();
        return 0;
    }
    const auto& settings = launch.config.values;
    const bool debug = settings.debugger.enabled;
    diagnostics::TraceStore store;
    store.SetExecutionEnabled(settings.diagnostics.executionTrace);
    TraceLogging logging(store);
    debugger::Debugger debugger(settings.debugger.breakOnEntry);
    Emulator emulator(&store, debug ? &debugger : nullptr, settings);

    if (!emulator.Initialise() || !emulator.LoadTitle(launch.title)) {
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

    if (settings.diagnostics.profiling)
        std::cout << diagnostics::Performance::Read().Report();
    return frontendFailed ? 1 : result.load(std::memory_order_acquire);
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
