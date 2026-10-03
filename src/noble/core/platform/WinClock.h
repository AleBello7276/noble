#pragma once

#include "core/HostClock.h"

class WinClock final : public HostClock {
public:
    // get cached performance counter frequency
    uint64_t GetTickFrequency() const noexcept override;

    // get windows performance counter
    uint64_t GetTickCount() const override;

private:
    friend class HostClock;
    WinClock();

    uint64_t frequency_;
};
