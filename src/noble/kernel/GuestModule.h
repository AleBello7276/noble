#pragma once

#include <cstddef>
#include <cstdint>

// guest loader record referenced by the executable module handle with all scalars stored big endian
struct GuestModule {
    uint32_t loadOrderLinks[2];
    uint32_t memoryOrderLinks[2];
    uint32_t initializationOrderLinks[2];
    uint32_t dllBase;
    uint32_t imageBase;
    uint32_t imageSize;
    struct UnicodeString {
        uint16_t length;
        uint16_t maximumLength;
        uint32_t buffer;
    } fullName, baseName;
    uint32_t flags;
    uint32_t fullImageSize;
    uint32_t entryPoint;
    uint16_t loadCount;
    uint16_t moduleIndex;
    uint32_t originalDllBase;
    uint32_t checksum;
    uint32_t loadFlags;
    uint32_t timestamp;
    uint32_t loadedImports;
    uint32_t xexHeaderBase;
    uint32_t closureRoot;
    uint32_t traversalParent;
};

static_assert(sizeof(GuestModule) == 0x64);
static_assert(offsetof(GuestModule, imageBase) == 0x1C);
static_assert(offsetof(GuestModule, entryPoint) == 0x3C);
static_assert(offsetof(GuestModule, xexHeaderBase) == 0x58);
