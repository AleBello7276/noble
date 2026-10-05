#pragma once

#include "kernel/GuestPointer.h"
#include "kernel/hle/Exports.h"

struct GuestKernelThread;

namespace hle::krnl {

// fixed xex header followed by optional key and value pairs
struct XexHeader {
    be<uint32_t> magic;
    be<uint32_t> moduleFlags;
    be<uint32_t> headerSize;
    be<uint32_t> reserved;
    be<uint32_t> securityOffset;
    be<uint32_t> optionalCount;
};
static_assert(sizeof(XexHeader) == 0x18);

// counted ansi string referencing guest memory without owning or copying the character buffer
struct AnsiString {
    be<uint16_t> length;
    be<uint16_t> maximumLength;
    GuestPointer<const char> buffer;
};
static_assert(sizeof(AnsiString) == 8);
static_assert(offsetof(AnsiString, buffer) == 4);

// initialize an ansi descriptor from an optional terminated source without allocating or copying it
void RtlInitAnsiString(Memory& memory, Pointer<AnsiString> destination, Pointer<const char> source);

// guest critical section layout with scalar fields encoded big endian
struct CriticalSection {
    uint8_t type;
    uint8_t spinCount;
    uint8_t size;
    uint8_t inserted;
    be<uint32_t> signalState;
    GuestPointer<void> waitList[2];
    be<int32_t> lockCount;
    be<int32_t> recursionCount;
    // guest kthread pointer rather than a thread id or host address
    GuestPointer<GuestKernelThread> owningThread;
};
static_assert(sizeof(CriticalSection) == 28);
static_assert(alignof(CriticalSection) == 1);
static_assert(offsetof(CriticalSection, lockCount) == 0x10);
static_assert(offsetof(CriticalSection, recursionCount) == 0x14);
static_assert(offsetof(CriticalSection, owningThread) == 0x18);

// build an unlocked guest critical section with a saturated spin count in units of 256
CriticalSection MakeCriticalSection(uint32_t spinCount = 0);

uint32_t RtlNtStatusToDosError(uint32_t status);

// expose the typed runtime library implementations for kernel export registration
std::span<const Export> RtlExports();

}  // namespace hle::krnl
