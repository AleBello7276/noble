#include "Logger.h"
#include "config/Config.h"
#include "debugger/Debugger.h"
#include "diagnostics/Performance.h"
#include "diagnostics/TraceStore.h"
#include "emulator/Emulator.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

int main(int argc, char* argv[]) try {
    const auto launch = config::ParseLaunch(argc, argv, config::Frontend::Profile);
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
    const auto origin = std::chrono::steady_clock::now();
    auto elapsed
        = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - origin).count(); };
    auto phase = [&](const char* name) { std::cerr << name << " at " << elapsed() << " s\n"; };
    diagnostics::TraceStore trace;
    trace.SetExecutionEnabled(settings.diagnostics.executionTrace);

    bool dumpIR = settings.jit.dumpIR;
    Logger::SetSink(
        [](LogLevel level, std::string_view message, void* context) noexcept {
            if (level >= LogLevel::Warn || *static_cast<const bool*>(context))
                std::cerr << message << '\n';
        },
        &dumpIR);
    // restore logging before the local sink context is destroyed on every exit path
    struct LoggingGuard {
        ~LoggingGuard() { Logger::SetSink(nullptr); }
    } loggingGuard;

    debugger::Debugger debugger(settings.debugger.breakOnEntry);
    Emulator emulator(&trace, debug ? &debugger : nullptr, settings);
    phase("constructed");

    if (!emulator.Initialise())
        return 1;

    phase("initialized");
    if (!emulator.LoadTitle(launch.title))
        return 1;

    phase("loaded");
    std::atomic_int result{-1};
    std::jthread execution([&] {
        try {
            result.store(emulator.Run() ? 0 : 1);
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            result.store(1);
        }
    });

    double previousReport = elapsed();
    bool fault = false;

    while (result.load() == -1 && elapsed() < 30) {
        if (debug) {
            for (const auto& thread : debugger.Threads())
                if (thread.status == debugger::ThreadStatus::Faulted)
                    fault = true;
        }
        for (const auto& thread : trace.Read().threads)
            if (thread.faulted)
                fault = true;
        if (fault)
            break;
        if (elapsed() - previousReport > 3) {
            previousReport = elapsed();
            phase("progress");
            std::cerr << diagnostics::Performance::Read().Report() << std::flush;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    const bool timeout = !fault && result.load() == -1;
    phase(fault ? "guest fault" : timeout ? "timeout" : "completed");
    std::cerr << diagnostics::Performance::Read().Report() << std::flush;
    emulator.RequestStop();
    execution.join();
    emulator.Shutdown();
    phase("shutdown");

    Logger::SetSink(nullptr);
    return timeout ? 2 : fault || result.load() == 1 ? 1 : 0;
} catch (const std::exception& error) {
    Logger::SetSink(nullptr);
    std::cerr << error.what() << '\n';
    return 1;
}
