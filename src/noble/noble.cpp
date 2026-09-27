#include "Loader/ImageLoader.h"
#include "Loader/PEImage.h"
#include "Loader/XEXImage.h"
#include "Logger.h"

#include "PPCModule.h"

int main(int argc, char* argv[]) {
    PPCModule dolphin = PPCModule("F:/Stuff/noble/test/dolphin/dolphin.xex", false, false);

    // Splash texts
    printf("Hello, World!\n");
    printf("Say hi to the new galaxy note\n");

    // 19/02/2025
    printf("Trans rights!!!  @permdog99\n");
    printf("Live and learn  @ashrindy\n");
    printf("On dog  @.nover.\n");
    printf("Gotta Go Fast  @neoslyde\n");
}
