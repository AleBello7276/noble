#pragma once

#include "kernel/GuestPointer.h"
#include "kernel/hle/Exports.h"
#include <cstddef>

namespace hle::krnl {

// guest registration record with a callback address and big endian priority
struct TitleTerminateRegistration {
    GuestPointer<void> notificationRoutine;
    be<uint32_t> priority;
    GuestPointer<void> listLinks[2];
};
static_assert(sizeof(TitleTerminateRegistration) == 0x10);
static_assert(offsetof(TitleTerminateRegistration, priority) == 0x4);
static_assert(offsetof(TitleTerminateRegistration, listLinks) == 0x8);

// expose the typed executable and module implementations for kernel export registration
std::span<const Export> ModuleExports();

}  // namespace hle::krnl
