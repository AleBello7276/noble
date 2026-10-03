#include "Rtl.h"

#include "kernel/Kernel.h"
#include <algorithm>
#include <array>

namespace hle::krnl {

enum X_OBJECT_TYPES : uint8_t {
    EventNotificationObject = 0x0,     // Manual Reset
    EventSynchronizationObject = 0x1,  // Auto Reset
    MutantObject = 0x2,
    ProcessObject = 0x3,
    QueueObject = 0x4,
    SemaphoreObject = 0x5,
    ThreadObject = 0x6,
    Spare1Object = 0x7,  // GateObject?
    TimerNotificationObject = 0x8,
    TimerSynchronizationObject = 0x9,
    Spare2Object = 0xA,
    Spare3Object = 0xB,
    Spare4Object = 0xC,
    Spare5Object = 0xD,
    Spare6Object = 0xE,
    Spare7Object = 0xF,
    Spare8Object = 0x10,
    Spare9Object = 0x11,
    ApcObject = 0x12,
    DpcObject = 0x13,
    DeviceQueueObject = 0x14,
    EventPairObject = 0x15,
    InterruptObject = 0x16,
    ProfileObject = 0x17,
    UndefinedObject = 0xFF,
};

CriticalSection MakeCriticalSection(uint32_t spinCount) {
    CriticalSection section{};

    section.type = X_OBJECT_TYPES::EventSynchronizationObject;
    section.spinCount = static_cast<uint8_t>(std::min<uint64_t>((uint64_t(spinCount) + 255) >> 8, 255));
    section.lockCount = -1;

    return section;
}

namespace {

// initialize an unlocked guest critical section without consuming or returning a status value
void RtlInitializeCriticalSection(Pointer<CriticalSection> section) {
    *section = MakeCriticalSection();
}

// initialize an unlocked guest critical section with a rounded spin count and return success
uint32_t RtlInitializeCriticalSectionAndSpinCount(Pointer<CriticalSection> section, uint32_t spinCount) {
    *section = MakeCriticalSection(spinCount);
    return 0;
}

// acquire the section recursively or return to the dispatcher with a prepared scheduler wait
void RtlEnterCriticalSection(Kernel& kernel, KThread& thread, PPCContext& cpu,
                             Pointer<CriticalSection> section) {
    if (!kernel.EnterCriticalSection(thread, section))
        cpu.Action = HostAction::Wait;
}

// acquire an available section without blocking and report whether acquisition succeeded
uint32_t RtlTryEnterCriticalSection(Kernel& kernel, KThread& thread, Pointer<CriticalSection> section) {
    return kernel.EnterCriticalSection(thread, section, true);
}

// release one recursive acquisition and wake the next waiter on the final release
void RtlLeaveCriticalSection(Kernel& kernel, KThread& thread, Pointer<CriticalSection> section) {
    kernel.LeaveCriticalSection(thread, section);
}

// find an optional xex header field and return its inline value or guest storage address
uint32_t RtlImageXexHeaderField(Memory& memory, Pointer<const XexHeader> header, uint32_t field) {
    if (!header)
        return 0;

    constexpr uint32_t fixedHeaderSize = sizeof(XexHeader);
    const GuestAddress headerAddress = header.guest_address();
    if (header->magic != 0x58455832)
        throw std::invalid_argument("invalid xex header magic");

    const uint32_t headerSize = header->headerSize;
    const uint32_t count = header->optionalCount;
    const uint64_t tableEnd = fixedHeaderSize + uint64_t(count) * 8;
    if (tableEnd > headerSize)
        throw std::out_of_range("xex optional header table exceeds header bounds");

    if (!memory.IsAccessible(headerAddress, headerSize))
        throw std::out_of_range("xex header is not in readable guest memory");
    const auto* bytes = static_cast<const uint8_t*>(memory.Translate(headerAddress, headerSize));
    const auto readWord
        = [&](uint32_t offset) { return uint32_t(*reinterpret_cast<const be<uint32_t>*>(bytes + offset)); };

    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t entry = fixedHeaderSize + i * 8;
        if (readWord(entry) != field)
            continue;

        const uint32_t value = readWord(entry + 4);
        const uint32_t format = field & 0xFF;
        if (format == 0)
            return value;
        if (format == 1)
            return headerAddress + entry + 4;

        // offset fields point to the block start including its size word for variable length fields
        const uint32_t minimumSize = format == 0xFF ? 4 : format * 4;
        if (value > headerSize || minimumSize > headerSize - value)
            throw std::out_of_range("xex optional header field exceeds header bounds");
        if (format == 0xFF) {
            const uint32_t size = readWord(value);
            if (size < 4 || size > headerSize - value)
                throw std::out_of_range("invalid xex optional header field size");
        }
        return headerAddress + value;
    }

    return 0;
}

constexpr std::array exports{
    Bind<&RtlEnterCriticalSection>(XboxLibrary::XboxKrnl, "RtlEnterCriticalSection"),
    Bind<&RtlTryEnterCriticalSection>(XboxLibrary::XboxKrnl, "RtlTryEnterCriticalSection"),
    Bind<&RtlLeaveCriticalSection>(XboxLibrary::XboxKrnl, "RtlLeaveCriticalSection"),
    Bind<&RtlInitializeCriticalSection>(XboxLibrary::XboxKrnl, "RtlInitializeCriticalSection"),
    Bind<&RtlInitializeCriticalSectionAndSpinCount>(XboxLibrary::XboxKrnl,
                                                    "RtlInitializeCriticalSectionAndSpinCount"),
    Bind<&RtlImageXexHeaderField>(XboxLibrary::XboxKrnl, "RtlImageXexHeaderField"),
};

}  // namespace

std::span<const Export> RtlExports() {
    return exports;
}

}  // namespace hle::krnl
