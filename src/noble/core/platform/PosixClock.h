#pragma once

#include "core/HostClock.h"

class PosixClock final : public HostClock {
public:
    // one billion ticks per second because the counter uses nanoseconds
    uint64_t GetTickFrequency() const noexcept override;

    // read clock_monotonic in nanoseconds or throw on a host api failure
    uint64_t GetTickCount() const override;

private:
    friend class HostClock;
    PosixClock() = default;
};
