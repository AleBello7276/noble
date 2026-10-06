#include "debugger/Debugger.h"
#include "emulator/Emulator.h"
#include <atomic>
#include <iostream>
#include <thread>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "usage: noble-debug <title.xex>\n";
        return 1;
    }
    debugger::Debugger debugger;
    Emulator emulator(nullptr, &debugger);
    if (!emulator.Initialise() || !emulator.LoadTitle(argv[1]))
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
    std::cout << "guest debugger, entry starts paused; enter help for commands\n";
    for (std::string command; std::getline(std::cin, command);) {
        if (command == "quit")
            break;
        std::cout << debugger.Command(command) << std::flush;
    }
    emulator.RequestStop();
    execution.join();
    emulator.Shutdown();
    return result.load();
}
