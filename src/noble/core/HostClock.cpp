#include "HostClock.h"

#ifdef _WIN32
#include "platform/WinClock.h"
#else
#include "platform/PosixClock.h"
#endif

HostClock& HostClock::GetInstance() {
#ifdef _WIN32
    static WinClock clock;
#else
    static PosixClock clock;
#endif
    return clock;
}
