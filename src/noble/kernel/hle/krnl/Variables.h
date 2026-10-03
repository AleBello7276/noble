#pragma once

#include "Rtl.h"
#include "kernel/hle/Shims.h"
#include <atomic>

namespace hle::krnl {

// exported kernel version with big endian sixteen bit components
struct KernelVersion {
    be<uint16_t> major;
    be<uint16_t> minor;
    be<uint16_t> build;
    be<uint16_t> qfe;
};
static_assert(sizeof(KernelVersion) == 8);

// exported console hardware record with a big endian flags word
struct HardwareInfo {
    be<uint32_t> flags;
    uint8_t processorCount;
    uint8_t pciBridgeRevision;
    uint8_t reserved[6];
    be<uint16_t> bootloaderMagic;
    be<uint16_t> bootloaderFlags;
};
static_assert(sizeof(HardwareInfo) == 16);

// shared guest clock values in big endian with times expressed in one hundred nanosecond units
struct alignas(8) TimeStampBundle {
    alignas(std::atomic_ref<be<uint64_t>>::required_alignment) be<uint64_t> interruptTime;
    alignas(std::atomic_ref<be<uint64_t>>::required_alignment) be<uint64_t> systemTime;
    alignas(std::atomic_ref<be<uint32_t>>::required_alignment) be<uint32_t> tickCount;
    be<uint32_t> padding;
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
