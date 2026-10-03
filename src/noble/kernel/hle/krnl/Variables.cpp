#include "Variables.h"

#include "core/byte_swap.h"
#include "cpu/Scheduler.h"
#include <array>
#include <atomic>
#include <cstring>
#include <format>

namespace hle::krnl {

void RegisterVariables(Registry& registry) {
    // a null monitor pointer indicates that no kernel debug monitor is attached
    registry.DefineVariable<uint32_t>(XboxLibrary::XboxKrnl, "KeDebugMonitorData", 0);
    // a null monitor pointer indicates that no kernel certification monitor is attached
    registry.DefineVariable<uint32_t>(XboxLibrary::XboxKrnl, "KeCertMonitorData", 0);
    registry.DefineVariable<uint32_t>(XboxLibrary::XboxKrnl, "XexExecutableModuleHandle", 0);

    registry.DefineVariable<uint32_t>(XboxLibrary::XboxKrnl, "VdGpuClockInMHz", 500);

    // the guest may populate this device record before publishing its own device pointer
    registry.DefineVariable<uint32_t>(XboxLibrary::XboxKrnl, "VdGlobalDevice",
                                      registry.AllocateVariableStorage(40));
    registry.DefineVariable<uint32_t>(XboxLibrary::XboxKrnl, "VdGlobalXamDevice", 0);

    CriticalSection calibrationLock{};
    calibrationLock.type = 1;
    calibrationLock.spinCount = (10000 + 255) >> 8;
    calibrationLock.lockCount = byte_swap(UINT32_MAX);
    registry.DefineVariableBytes(XboxLibrary::XboxKrnl, "VdHSIOCalibrationLock",
                                 std::as_bytes(std::span{&calibrationLock, 1}));

    // use the same default kernel version as xenia until noble exposes version configuration
    const KernelVersion version{byte_swap(uint16_t(2)), 0, byte_swap(uint16_t(1888)), 0};
    registry.DefineVariableBytes(XboxLibrary::XboxKrnl, "XboxKrnlVersion",
                                 std::as_bytes(std::span{&version, 1}));

    HardwareInfo hardware{};
    hardware.flags = byte_swap(uint32_t(0x20));
    hardware.processorCount = Scheduler::kProcessorCount;
    registry.DefineVariableBytes(XboxLibrary::XboxKrnl, "XboxHardwareInfo",
                                 std::as_bytes(std::span{&hardware, 1}));

    std::array<char, 1024> commandLine{};
    registry.DefineVariableBytes(XboxLibrary::XboxKrnl, "ExLoadedCommandLine",
                                 std::as_bytes(std::span{commandLine}));
    UpdateCommandLine(registry, "default.xex");

    const TimeStampBundle timestamp{};
    registry.DefineVariableBytes(XboxLibrary::XboxKrnl, "KeTimeStampBundle",
                                 std::as_bytes(std::span{&timestamp, 1}));
}

void UpdateTimeStampBundle(TimeStampBundle& bundle, uint64_t interruptTime, uint64_t systemTime) {
    std::atomic_ref(bundle.interruptTime).store(byte_swap(interruptTime), std::memory_order_relaxed);
    std::atomic_ref(bundle.systemTime).store(byte_swap(systemTime), std::memory_order_relaxed);
    std::atomic_ref(bundle.tickCount)
        .store(byte_swap(static_cast<uint32_t>(interruptTime / 10000)), std::memory_order_relaxed);
}

void UpdateCommandLine(Registry& registry, std::string_view imagePath) {
    const auto separator = imagePath.find_last_of("/\\");
    const auto filename = separator == std::string_view::npos ? imagePath : imagePath.substr(separator + 1);
    const auto quoted = std::format("\"{}\"", filename);
    std::array<char, 1024> commandLine{};
    if (quoted.size() >= commandLine.size())
        throw std::length_error("guest executable command line is too long");
    std::memcpy(commandLine.data(), quoted.data(), quoted.size());
    registry.UpdateVariableBytes(XboxLibrary::XboxKrnl, "ExLoadedCommandLine",
                                 std::as_bytes(std::span{commandLine}));
}

}  // namespace hle::krnl
