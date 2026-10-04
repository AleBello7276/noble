#pragma once

#include <cstdint>
#include <mutex>

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

    // get guest ticks per second
    uint64_t GetGuestTickFreq() const noexcept { return mGuestTickFrequency_; }

    uint64_t GetGuestTickCount();

    // change guest clock speed
    void SetGuestClockScale(uint32_t numerator, uint32_t denominator = 1);

private:
    uint64_t mGuestTickFrequency_ = 0;
    std::mutex mTickMutex_;
    uint64_t mLastHostTickCount_ = 0;
    uint64_t mLastGuestTickCount_ = 0;
    uint32_t mGuestTickNumerator_ = 1;
    uint32_t mGuestTickDenominator_ = 1;
    uint64_t mGuestTickRemainder_ = 0;

    // update the guest counter
    uint64_t UpdateGuestClock(uint64_t hostTickCount);

protected:
    HostClock() = default;
};
