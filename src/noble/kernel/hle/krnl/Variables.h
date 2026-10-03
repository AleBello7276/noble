#pragma once

#include "kernel/hle/Shims.h"

namespace hle::krnl {

// guest critical section layout with scalar fields encoded big endian
struct CriticalSection {
    uint8_t type;
    uint8_t spinCount;
    uint8_t size;
    uint8_t inserted;
    uint32_t signalState;
    uint32_t waitList[2];
    uint32_t lockCount;
    uint32_t recursionCount;
    uint32_t owningThread;
};
static_assert(sizeof(CriticalSection) == 28);
static_assert(offsetof(CriticalSection, lockCount) == 0x10);

// exported kernel version with big endian sixteen bit components
struct KernelVersion {
    uint16_t major;
    uint16_t minor;
    uint16_t build;
    uint16_t qfe;
};
static_assert(sizeof(KernelVersion) == 8);

// exported console hardware record with a big endian flags word
struct HardwareInfo {
    uint32_t flags;
    uint8_t processorCount;
    uint8_t pciBridgeRevision;
    uint8_t reserved[6];
    uint16_t bootloaderMagic;
    uint16_t bootloaderFlags;
};
static_assert(sizeof(HardwareInfo) == 16);

// shared guest clock values in big endian with times expressed in one hundred nanosecond units
struct alignas(8) TimeStampBundle {
    uint64_t interruptTime;
    uint64_t systemTime;
    uint32_t tickCount;
    uint32_t padding;
};
static_assert(sizeof(TimeStampBundle) == 24);
static_assert(offsetof(TimeStampBundle, tickCount) == 0x10);

// allocate guest storage for implemented kernel variable exports
void RegisterVariables(Registry& registry);

// update each clock field atomically while guest code may read the shared bundle
void UpdateTimeStampBundle(TimeStampBundle& bundle, uint64_t interruptTime, uint64_t systemTime);

// write a quoted executable filename into the fixed guest command line buffer
void UpdateCommandLine(Registry& registry, std::string_view imagePath);

}  // namespace hle::krnl
