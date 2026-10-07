#include "Video.h"

#include "kernel/Kernel.h"
#include "gpu/CommandPackets.h"
#include "gpu/Registers.h"
#include <algorithm>
#include <array>
#include <numeric>
#include <new>
#include <cstring>
#include <stdexcept>

namespace hle::krnl {

void QueryVideoMode(Kernel& kernel, VideoMode& videoMode) {
    const auto mode = kernel.GPU().GetDisplayMode();
    videoMode = {};
    videoMode.displayWidth = mode.width;
    videoMode.displayHeight = mode.height;
    videoMode.isInterlaced = mode.interlaced;
    videoMode.isWidescreen = mode.widescreen;
    videoMode.isHiDef = uint32_t(videoMode.displayWidth) >= 0x500;
    videoMode.refreshRate = mode.refreshRate;
    videoMode.videoStandard = 1;
    videoMode.pixelRate = 0x8A;
    videoMode.widescreenFlag = uint32_t(videoMode.isWidescreen) ? 0x01 : 0x03;
}

// write the current video mode to the caller supplied guest structure
void VdQueryVideoMode(Kernel& kernel, Pointer<VideoMode> videoMode) {
    QueryVideoMode(kernel, *videoMode);
}

// report backend dimensions and refresh rate with the same initial scaler and overscan defaults as xenia
void VdGetCurrentDisplayInformation(Kernel& kernel, Pointer<DisplayInformation> displayInformation) {
    VideoMode mode;
    QueryVideoMode(kernel, mode);
    auto& display = *displayInformation;
    display = {};
    display.frontBufferWidth = static_cast<uint16_t>(mode.displayWidth);
    display.frontBufferHeight = static_cast<uint16_t>(mode.displayHeight);
    display.scaler.sourceRect.x2 = mode.displayWidth;
    display.scaler.sourceRect.y2 = mode.displayHeight;
    display.scaler.outputWidth = mode.displayWidth;
    display.scaler.outputHeight = mode.displayHeight;
    display.scaler.horizontalFilterType = 1;
    display.scaler.verticalFilterType = 1;
    display.overscanLeft = display.overscanRight = 320;
    display.overscanTop = display.overscanBottom = 180;
    display.displayWidth = static_cast<uint16_t>(mode.displayWidth);
    display.displayHeight = static_cast<uint16_t>(mode.displayHeight);
    display.refreshRate = mode.refreshRate;
    display.isInterlaced = mode.isInterlaced;
    display.actualDisplayWidth = static_cast<uint16_t>(mode.displayWidth);
}

// return success without changing display hardware while video mode queries use backend defaults
// flags select sd output at bit 1, ten bit color at bit 27 and color space at bits 28 through 29
uint32_t VdSetDisplayMode([[maybe_unused]] uint32_t flags) {
    return 0;
}

// write the initial display gamma defaults as big endian guest values
// type 2 selects bt709 and the power is only used when type 3 is selected
void VdGetCurrentDisplayGamma(Pointer<be<uint32_t>> type, Pointer<be<float>> power) {
    *type = 2;
    *power = 2.22222233f;
}

// return video flags with bit 0 for widescreen and bits 1 and 2 for widths of at least 1280 and 1920
uint32_t VdQueryVideoFlags(Kernel& kernel) {
    const auto mode = kernel.GPU().GetDisplayMode();
    uint32_t flags = mode.widescreen ? 1u : 0u;
    if (mode.width >= 1280)
        flags |= 2u;
    if (mode.width >= 1920)
        flags |= 4u;
    return flags;
}

// pass the opaque guest engine parameters to the selected graphics backend
uint32_t VdInitializeEngines(Kernel& kernel, uint32_t unknown, GuestAddress callback,
                             GuestAddress callbackArgument, GuestAddress pfpMicrocode,
                             GuestAddress meMicrocode) {
    return kernel.GPU().InitializeEngines({unknown, callback, callbackArgument, pfpMicrocode, meMicrocode});
}

// stop guest engine state while retaining the host graphics backend
void VdShutdownEngines(Kernel& kernel) {
    kernel.GPU().ShutdownEngines();
}

void VdInitializeRingBuffer(Kernel& kernel, Memory& memory, GuestAddress physicalAddress, int32_t sizeLog2) {
    if (sizeLog2 < 0 || sizeLog2 > 26)
        throw std::invalid_argument("invalid gpu ring buffer size encoding");

    const uint32_t size = uint32_t(1) << (uint32_t(sizeLog2) + 3);
    if (physicalAddress > 0x20000000u - size)
        throw std::out_of_range("gpu ring buffer exceeds physical memory");

    // use a physical alias so physical address zero is valid and permissions are checked
    const GuestAddress address = 0xA0000000u + physicalAddress;
    if (!memory.IsAccessible(address, size, true))
        throw std::out_of_range("gpu ring buffer is not in writable physical memory");

    std::memset(memory.Translate(address, size), 0, size);
    kernel.GPU().InitializeRingBuffer({physicalAddress, uint32_t(sizeLog2)});
}

// configure future physical read pointer writes without changing guest memory during registration
void VdEnableRingBufferRPtrWriteBack(Kernel& kernel, Memory& memory, GuestAddress physicalAddress,
                                     int32_t blockSizeLog2) {
    if (blockSizeLog2 < 0 || blockSizeLog2 > 19)
        throw std::invalid_argument("invalid gpu read pointer writeback block encoding");

    if (physicalAddress) {
        if (physicalAddress > 0x1FFFFFFCu
            || !memory.IsAccessible(0xA0000000u + physicalAddress, sizeof(uint32_t), true))
            throw std::out_of_range("gpu read pointer writeback is not in writable physical memory");
    }

    kernel.GPU().EnableReadPointerWriteBack({physicalAddress, uint32_t(blockSizeLog2)});
}

// retain guest callback addresses without invoking or dereferencing them during registration
void VdSetGraphicsInterruptCallback(Kernel& kernel, GuestAddress callback, GuestAddress userData) {
    kernel.SetGraphicsInterruptCallback(callback, userData);
}

// fill the opaque system command buffer record with null gpu placeholder identifiers
// the first output spans 0x94 bytes and the second output is one big endian word
// these identifiers are tokens rather than mapped guest addresses or gpu commands
void VdGetSystemCommandBuffer(Pointer<std::array<be<uint32_t>, 0x94 / sizeof(uint32_t)>> buffer,
                              Pointer<be<uint32_t>> identifier) {
    auto& record = *buffer;
    auto& token = *identifier;
    record = {};
    record[0] = 0xBEEF0000;
    token = 0xBEEF0001;
}

// write a front buffer texture fetch and emulator swap packet into the caller reserved 64 words
// the fetch header contains a guest virtual address which is replaced with a physical gpu address
// opaque system buffer arguments are retained in the abi but not dereferenced by this implementation
void VdSwap(Memory& memory, Pointer<std::array<be<uint32_t>, 64>> buffer,
            Pointer<const std::array<be<uint32_t>, 6>> fetch,
            [[maybe_unused]] GuestAddress systemWriteback,
            [[maybe_unused]] GuestAddress systemCommandBuffer,
            [[maybe_unused]] GuestAddress systemIdentifier,
            Pointer<const be<uint32_t>> frontBuffer, Pointer<const be<uint32_t>> textureFormat,
            Pointer<const be<uint32_t>> colorSpace, Pointer<const be<uint32_t>> width,
            Pointer<const be<uint32_t>> height) {
    auto& destination = *buffer;
    const auto texture = *fetch;
    const uint32_t address = uint32_t(texture[1]) & 0xFFFFF000;
    const uint32_t physicalAddress = memory.GetPhysicalAddress(address);
    const uint32_t displayWidth = *width;
    const uint32_t displayHeight = *height;
    const uint32_t format = *textureFormat;

    if (uint32_t(*frontBuffer) != address)
        throw std::invalid_argument("swap front buffer does not match texture fetch address");
    if (physicalAddress == UINT32_MAX || !memory.IsAccessible(address, 1))
        throw std::out_of_range("swap front buffer is not in accessible physical memory");
    if ((format != 6 && format != 54) || uint32_t(*colorSpace) != 0)
        throw std::invalid_argument("unsupported swap texture format or color space");
    if (displayWidth != 1 + (uint32_t(texture[2]) & 0x1FFF)
        || displayHeight != 1 + ((uint32_t(texture[2]) >> 13) & 0x1FFF))
        throw std::invalid_argument("swap dimensions do not match texture fetch");

    // build locally so invalid inputs cannot leave a partial command and inputs may alias the output
    std::array<be<uint32_t>, 64> commands;
    commands.fill(be<uint32_t>(gpu::MakePacketType2()));
    commands[0] = gpu::MakePacketType0(uint32_t(gpu::Register::SHADER_CONSTANT_FETCH_00_0), 6);
    for (size_t i = 0; i < texture.size(); ++i)
        commands[i + 1] = texture[i];
    commands[2] = (uint32_t(texture[1]) & 0xFFF) | physicalAddress;
    commands[7] = gpu::MakePacketType3(gpu::kSwapOpcode, 4);
    commands[8] = gpu::kSwapSignature;
    commands[9] = physicalAddress;
    commands[10] = displayWidth;
    commands[11] = displayHeight;
    destination = commands;
}

// fill the caller requested word count with nop packets until hardware scaler commands are implemented
// packed coordinates and sizes contain x or width in the low half and y or height in the high half
// preserve the full twelve argument abi including filter pointers and the destination on the guest stack
uint32_t VdInitializeScalerCommandBuffer(
    Kernel& kernel, Memory& memory, [[maybe_unused]] uint32_t sourceXY,
    [[maybe_unused]] uint32_t sourceWH, [[maybe_unused]] uint32_t outputXY, uint32_t outputWH,
    [[maybe_unused]] uint32_t frontBufferWH, [[maybe_unused]] uint32_t verticalFilterType,
    [[maybe_unused]] Pointer<const DisplayFilterParameters> verticalFilter,
    [[maybe_unused]] uint32_t horizontalFilterType,
    [[maybe_unused]] Pointer<const DisplayFilterParameters> horizontalFilter,
    [[maybe_unused]] GuestAddress unknown, GuestAddress destination, uint32_t wordCount) {
    if (!wordCount)
        return 0;

    const uint64_t byteCount = uint64_t(wordCount) * sizeof(uint32_t);
    if (!destination || byteCount > UINT32_MAX
        || !memory.IsAccessible(destination, static_cast<size_t>(byteCount), true))
        throw std::out_of_range("scaler command buffer is not in writable guest memory");

    const uint32_t outputWidth = outputWH & 0xFFFF;
    const uint32_t outputHeight = outputWH >> 16;
    const auto mode = kernel.GPU().GetDisplayMode();
    if (!outputWidth || !outputHeight || !mode.width || !mode.height)
        throw std::invalid_argument("scaler output and display dimensions must be nonzero");

    // account for the output rectangle relative to the full display without reversing packed width and height
    uint64_t aspectWidth = uint64_t(mode.widescreen ? 16 : 4) * outputWidth * mode.height;
    uint64_t aspectHeight = uint64_t(mode.widescreen ? 9 : 3) * outputHeight * mode.width;
    const uint64_t factor = std::gcd(aspectWidth, aspectHeight);
    aspectWidth /= factor;
    aspectHeight /= factor;

    auto* words = static_cast<be<uint32_t>*>(memory.Translate(destination, static_cast<size_t>(byteCount)));
    std::fill_n(words, wordCount, be<uint32_t>(gpu::MakePacketType2()));
    kernel.GPU().SetScaledAspectRatio({aspectWidth, aspectHeight});
    return wordCount;
}

// return a guest owned physical allocation that can later be released by mmfreephysicalmemory
// the null gpu does not capture display contents and the opaque allocation has no guest access
uint32_t VdPersistDisplay(Memory& memory, [[maybe_unused]] uint32_t unknown,
                         Pointer<be<uint32_t>> persistedDisplay) {
    if (!persistedDisplay)
        return 1;

    GuestAddress address = 0;
    try {
        address = memory.AllocatePhysical(64, 32, GuestHeapKind::Physical4K, 0, 0x1FFFFFFF,
                                           MemoryProtection::NoAccess);
    } catch (const std::bad_alloc&) {
        *persistedDisplay = 0;
        return 0;
    }

    *persistedDisplay = address;
    return address ? 1 : 0;
}

// register the opaque guest identifier address without modifying guest memory
void VdSetSystemCommandBufferGpuIdentifierAddress(Kernel& kernel, GuestAddress address) {
    kernel.GPU().SetSystemCommandBufferGpuIdentifierAddress(address);
}

// TODO:
// return success until graphics notification registration and guest callback delivery are implemented
// notification is normally 1 and arguments is an opaque guest address for scaling parameters
uint32_t VdCallGraphicsNotificationRoutines([[maybe_unused]] uint32_t notification,
                                            [[maybe_unused]] GuestAddress arguments) {
    return 0;
}

// return success as a stub since the null gpu has no edram hardware to retrain
uint32_t VdRetrainEDRAM([[maybe_unused]] uint32_t unknown0, [[maybe_unused]] uint32_t unknown1,
                      [[maybe_unused]] uint32_t unknown2, [[maybe_unused]] uint32_t unknown3,
                      [[maybe_unused]] uint32_t unknown4, [[maybe_unused]] uint32_t unknown5) {
    return 0;
}

// return success as a stub since the null gpu has no edram hardware to retrain
uint32_t VdRetrainEDRAMWorker([[maybe_unused]] uint32_t unknown) {
    return 0;
}

// report successful hsio training as a stub since the null gpu has no physical link to train
uint32_t VdIsHSIOTrainingSucceeded() {
    return 1;
}

constexpr std::array exports{
    Bind<&VdGetCurrentDisplayInformation>(XboxLibrary::XboxKrnl, "VdGetCurrentDisplayInformation"),
    Bind<&VdSetDisplayMode>(XboxLibrary::XboxKrnl, "VdSetDisplayMode"),
    Bind<&VdQueryVideoMode>(XboxLibrary::XboxKrnl, "VdQueryVideoMode"),
    Bind<&VdInitializeScalerCommandBuffer>(XboxLibrary::XboxKrnl, "VdInitializeScalerCommandBuffer"),
    Bind<&VdPersistDisplay>(XboxLibrary::XboxKrnl, "VdPersistDisplay"),
    Bind<&VdSwap>(XboxLibrary::XboxKrnl, "VdSwap"),
    Bind<&VdGetSystemCommandBuffer>(XboxLibrary::XboxKrnl, "VdGetSystemCommandBuffer"),
    Bind<&VdGetCurrentDisplayGamma>(XboxLibrary::XboxKrnl, "VdGetCurrentDisplayGamma"),
    Bind<&VdQueryVideoFlags>(XboxLibrary::XboxKrnl, "VdQueryVideoFlags"),
    Bind<&VdIsHSIOTrainingSucceeded>(XboxLibrary::XboxKrnl, "VdIsHSIOTrainingSucceeded"),
    Bind<&VdRetrainEDRAM>(XboxLibrary::XboxKrnl, "VdRetrainEDRAM"),
    Bind<&VdRetrainEDRAMWorker>(XboxLibrary::XboxKrnl, "VdRetrainEDRAMWorker"),
    Bind<&VdCallGraphicsNotificationRoutines>(XboxLibrary::XboxKrnl, "VdCallGraphicsNotificationRoutines"),
    Bind<&VdInitializeEngines>(XboxLibrary::XboxKrnl, "VdInitializeEngines"),
    Bind<&VdInitializeRingBuffer>(XboxLibrary::XboxKrnl, "VdInitializeRingBuffer"),
    Bind<&VdEnableRingBufferRPtrWriteBack>(XboxLibrary::XboxKrnl, "VdEnableRingBufferRPtrWriteBack"),
    Bind<&VdShutdownEngines>(XboxLibrary::XboxKrnl, "VdShutdownEngines"),
    Bind<&VdSetGraphicsInterruptCallback>(XboxLibrary::XboxKrnl, "VdSetGraphicsInterruptCallback"),
    Bind<&VdSetSystemCommandBufferGpuIdentifierAddress>(XboxLibrary::XboxKrnl,
                                                        "VdSetSystemCommandBufferGpuIdentifierAddress"),
};

std::span<const Export> VideoExports() {
    return exports;
}

}  // namespace hle::krnl
