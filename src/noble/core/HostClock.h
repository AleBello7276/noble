#pragma once

#include <cstdint>

class HostClock {
public:
    static HostClock& GetInstance();

    virtual ~HostClock() = default;

    HostClock(const HostClock&) = delete;
    HostClock& operator=(const HostClock&) = delete;
    HostClock(HostClock&&) = delete;
    HostClock& operator=(HostClock&&) = delete;

    // host counter ticks per second
    virtual uint64_t GetTickFrequency() const noexcept = 0;

    // subtract two readings and divide by the frequency to measure elapsed seconds
    virtual uint64_t GetTickCount() const = 0;

protected:
    HostClock() = default;
};
