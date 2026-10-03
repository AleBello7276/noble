#include "WinClock.h"

#include <Windows.h>
#include <stdexcept>
#include <system_error>

WinClock::WinClock() {
    LARGE_INTEGER frequency{};

    if (!QueryPerformanceFrequency(&frequency))
        throw std::system_error(GetLastError(), std::system_category(), "QueryPerformanceFrequency failed");

    if (frequency.QuadPart <= 0)
        throw std::runtime_error("invalid host performance counter frequency");

    frequency_ = static_cast<uint64_t>(frequency.QuadPart);
}

uint64_t WinClock::GetTickFrequency() const noexcept {
    return frequency_;
}

uint64_t WinClock::GetTickCount() const {
    LARGE_INTEGER counter{};

    if (!QueryPerformanceCounter(&counter))
        throw std::system_error(GetLastError(), std::system_category(), "QueryPerformanceCounter failed");

    return static_cast<uint64_t>(counter.QuadPart);
}
