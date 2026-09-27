#include "Logger.h"
#include "emulator/Emulator.h"

int main(int argc, char* argv[]) {
    // if (argc != 2) {
    //     LOG_ERROR("Usage: noble <title.xex>");
    //     return 1;
    // }
    Emulator emu = Emulator();

    if (emu.Initialise() == false) {
        LOG_FATAL("Failed to Initialise emulator subsystems\n");
        return 1;
    }

    // test
    if (emu.LoadTitle("F:/Stuff/noble/test/dolphin/dolphin.xex") == false) {
        LOG_FATAL("Failed to load the Title\n");
        return 1;
    }

    if (!emu.Run()) {
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
}
