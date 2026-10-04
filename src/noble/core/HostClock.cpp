#include "HostClock.h"

#include <numeric>
#include <stdexcept>

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
        instance.mLastHostTickCount_ = instance.GetTickCount();
        instance.mLastGuestTickCount_ = instance.mLastHostTickCount_;
        return instance;
    }();
    return clock;
}

uint64_t HostClock::GetGuestTickCount() {
    std::unique_lock lock(mTickMutex_, std::defer_lock);
    if (lock.try_lock())
        return UpdateGuestClock(GetTickCount());

    // reuse the snapshot published by the thread that held the lock
    lock.lock();
    return mLastGuestTickCount_;
}

uint64_t HostClock::UpdateGuestClock(uint64_t hostTickCount) {
    // keep the last host sample if the host counter ever moves backward
    if (hostTickCount <= mLastHostTickCount_)
        return mLastGuestTickCount_;

    const uint64_t delta = hostTickCount - mLastHostTickCount_;
    mLastHostTickCount_ = hostTickCount;

    const uint64_t fractional
        = (delta % mGuestTickDenominator_) * mGuestTickNumerator_ + mGuestTickRemainder_;

    mLastGuestTickCount_
        += (delta / mGuestTickDenominator_) * mGuestTickNumerator_ + fractional / mGuestTickDenominator_;

    mGuestTickRemainder_ = fractional % mGuestTickDenominator_;

    return mLastGuestTickCount_;
}

void HostClock::SetGuestClockScale(uint32_t numerator, uint32_t denominator) {
    if (!denominator)
        throw std::invalid_argument("guest clock scale denominator must be nonzero");

    std::lock_guard lock(mTickMutex_);
    UpdateGuestClock(GetTickCount());

    const uint32_t divisor = std::gcd(numerator, denominator);

    numerator /= divisor;
    denominator /= divisor;

    if (numerator == mGuestTickNumerator_ && denominator == mGuestTickDenominator_)
        return;

    mGuestTickNumerator_ = numerator;
    mGuestTickDenominator_ = denominator;
    mGuestTickRemainder_ = 0;
}
