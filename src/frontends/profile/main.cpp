#include "Logger.h"
#include "debugger/Debugger.h"
#include "diagnostics/Performance.h"
#include "diagnostics/TraceStore.h"
#include "emulator/Emulator.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>

int main(int argc, char* argv[]) {
    if (argc < 2 || argc > 3 || (argc == 3 && std::string_view(argv[2]) != "--debug")) {
        std::cerr << "usage: noble-profile <title.xex> [--debug]\n";
        return 1;
    }

    const bool debug = argc == 3;
    const auto origin = std::chrono::steady_clock::now();
    auto elapsed
        = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - origin).count(); };
    auto phase = [&](const char* name) { std::cerr << name << " at " << elapsed() << " s\n"; };
    diagnostics::Performance::enabled.store(true);
    diagnostics::TraceStore trace;

    Logger::SetSink([](LogLevel level, std::string_view message, void*) noexcept {
        if (level >= LogLevel::Warn)
            std::cerr << message << '\n';
    });

    debugger::Debugger debugger(false);
    Emulator emulator(&trace, debug ? &debugger : nullptr);
    phase("constructed");

    if (!emulator.Initialise())
        return 1;

    phase("initialized");
    if (!emulator.LoadTitle(argv[1]))
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

    phase(fault ? "guest fault" : result.load() == -1 ? "timeout" : "completed");
    std::cerr << diagnostics::Performance::Read().Report() << std::flush;
    emulator.RequestStop();
    execution.join();
    emulator.Shutdown();
    phase("shutdown");

    Logger::SetSink(nullptr);
    return fault || result.load() == 1 ? 1 : 0;
}
