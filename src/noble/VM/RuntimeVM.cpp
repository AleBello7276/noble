#include "RuntimeVM.h"

#include "Logger.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include "Windows.h"
#endif

#define RESERVED_MEMORY_SIZE    0x100000000
#define RESERVED_MEMORY_START   0x100000000



void RuntimeVM::ReserveMemory() {
#ifdef _WIN32
    mMainMemory = VirtualAlloc((void*)RESERVED_MEMORY_START, RESERVED_MEMORY_SIZE, MEM_RESERVE, PAGE_READWRITE);
    if (mMainMemory == NULL) {
        mMainMemory = VirtualAlloc(NULL, RESERVED_MEMORY_SIZE, MEM_RESERVE, PAGE_READWRITE);
        if(mMainMemory == NULL) {
            LOG_FATAL("Unable to reserve memory for the Runtime VM");
        }
    }
#endif
}


RuntimeVM::RuntimeVM() {
    ReserveMemory();
}

RuntimeVM::~RuntimeVM() {
    // TODO: cleanup console memory and stuff
}
