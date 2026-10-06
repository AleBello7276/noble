#include "config/Config.h"
#include "debugger/Debugger.h"
#include "diagnostics/Performance.h"
#include "diagnostics/TraceStore.h"
#include "emulator/Emulator.h"
#include <atomic>
#include <iostream>
#include <thread>

int main(int argc, char* argv[]) try {
    const auto launch = config::ParseLaunch(argc, argv, config::Frontend::Debugger);
    if (launch.help) {
        std::cout << config::Help(argv[0]);
        return 0;
    }
    if (launch.printConfig) {
        std::cout << launch.config.Describe();
        return 0;
    }
    const auto& settings = launch.config.values;
    diagnostics::TraceStore trace;
    trace.SetExecutionEnabled(settings.diagnostics.executionTrace);
    debugger::Debugger debugger(settings.debugger.breakOnEntry);
    Emulator emulator(settings.diagnostics.executionTrace ? &trace : nullptr, &debugger, settings);
    if (!emulator.Initialise() || !emulator.LoadTitle(launch.title))
        return 1;
    std::atomic_int result{-1};
    std::jthread execution([&] {
        try {
            result.store(emulator.Run() ? 0 : 1);
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            result.store(1);
        }
    });
    std::cout << "guest debugger; enter help for commands\n";
    for (std::string command; std::getline(std::cin, command);) {
        if (command == "quit")
            break;
        std::cout << debugger.Command(command) << std::flush;
    }
    emulator.RequestStop();
    execution.join();
    emulator.Shutdown();
    if (settings.diagnostics.profiling)
        std::cout << diagnostics::Performance::Read().Report();
    return result.load();
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
