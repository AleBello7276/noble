#pragma once

#include "GuestPointer.h"
#include <cstddef>
#include <cstdint>

struct GuestListEntry;

// guest loader record referenced by the executable module handle with all scalars stored big endian
struct GuestModule {
    GuestPointer<GuestListEntry> loadOrderLinks[2];
    GuestPointer<GuestListEntry> memoryOrderLinks[2];
    GuestPointer<GuestListEntry> initializationOrderLinks[2];
    GuestPointer<void> dllBase;
    GuestPointer<void> imageBase;
    be<uint32_t> imageSize;
    struct UnicodeString {
        be<uint16_t> length;
        be<uint16_t> maximumLength;
        GuestPointer<be<uint16_t>> buffer;
    } fullName, baseName;
    be<uint32_t> flags;
    be<uint32_t> fullImageSize;
    GuestPointer<void> entryPoint;
    be<uint16_t> loadCount;
    be<uint16_t> moduleIndex;
    GuestPointer<void> originalDllBase;
    be<uint32_t> checksum;
    be<uint32_t> loadFlags;
    be<uint32_t> timestamp;
    GuestPointer<void> loadedImports;
    GuestPointer<void> xexHeaderBase;
    GuestPointer<GuestModule> closureRoot;
    GuestPointer<GuestModule> traversalParent;
};

static_assert(sizeof(GuestModule) == 0x64);
static_assert(offsetof(GuestModule, imageBase) == 0x1C);
static_assert(offsetof(GuestModule, entryPoint) == 0x3C);
static_assert(offsetof(GuestModule, xexHeaderBase) == 0x58);
