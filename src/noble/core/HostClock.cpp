#include "HostClock.h"

#ifdef _WIN32
#include "platform/WinClock.h"
#else
#include "platform/PosixClock.h"
#endif

HostClock& HostClock::GetInstance() {
    static HostClock& clock = []() -> HostClock& {
#ifdef _WIN32
        static WinClock instance;
#else
        static PosixClock instance;
#endif
        instance.mGuestTickFrequency_ = instance.GetTickFrequency();
        return instance;
    }();
    return clock;
}
