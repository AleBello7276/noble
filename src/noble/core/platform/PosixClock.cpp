#include "PosixClock.h"

#include <cerrno>
#include <system_error>
#include <time.h>

uint64_t PosixClock::GetTickFrequency() const noexcept {
    return 1000000000;
}

uint64_t PosixClock::GetTickCount() const {
    timespec time{};
    if (clock_gettime(CLOCK_MONOTONIC, &time) != 0)
        throw std::system_error(errno, std::generic_category(), "clock_gettime failed");

    return static_cast<uint64_t>(time.tv_sec) * GetTickFrequency() + static_cast<uint64_t>(time.tv_nsec);
}
