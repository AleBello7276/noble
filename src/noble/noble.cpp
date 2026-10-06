#include "Logger.h"
#include "config/Config.h"
#include "diagnostics/Performance.h"
#include "diagnostics/TraceStore.h"
#include "emulator/Emulator.h"
#include <iostream>

/* -- TODO List: --
    - Move this list somewhere else nicer.
    - the Xex/Exe Loader is not endian agnostic, it's not an issue for now but better to keep that in mind.
*/

int main(int argc, char* argv[]) try {
    const auto launch = config::ParseLaunch(argc, argv, config::Frontend::Normal, "test/dolphin/dolphin.xex");
    if (launch.help) {
        std::cout << config::Help(argv[0]);
        return 0;
    }
    if (launch.printConfig) {
        std::cout << launch.config.Describe();
        return 0;
    }
    const auto& settings = launch.config.values;
    if (settings.debugger.enabled)
        throw std::invalid_argument("use noble-debug or noble-tui for an instruction debugger session");
    diagnostics::TraceStore trace;
    trace.SetExecutionEnabled(settings.diagnostics.executionTrace);
    Emulator emu(settings.diagnostics.executionTrace ? &trace : nullptr, nullptr, settings);

    if (emu.Initialise() == false) {
        LOG_FATAL("Failed to Initialise emulator subsystems\n");
        return 1;
    }

    // test
    if (emu.LoadTitle(launch.title) == false) {
        LOG_FATAL("Failed to load the Title\n");
        return 1;
    }

    const bool success = emu.Run();
    if (settings.diagnostics.profiling)
        std::cout << diagnostics::Performance::Read().Report();
    if (settings.diagnostics.executionTrace)
        std::cout << "retained trace events: " << trace.Read().events.size() << '\n';
    if (!success) {
        LOG_FATAL("Guest execution failed");
        emu.Shutdown();
        return 1;
    }

    emu.Shutdown();

    //
    //
    printf("\n\n\n");
    // Splash texts
    printf("Hello, World!\n");
    printf("Say hi to the new galaxy note\n");

    // 19/02/2025
    printf("Trans rights!!!  @permdog99\n");
    printf("Live and learn  @ashrindy\n");
    printf("On dog  @.nover.\n");
    printf("Gotta Go Fast  @neoslyde\n");

    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
