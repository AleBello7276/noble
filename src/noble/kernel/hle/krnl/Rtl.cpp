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


struct StatusError {
    uint32_t status;
    uint32_t error;
};

constexpr StatusError statusErrors[] = {
#include "NtStatusToDosError.inc"
};

static_assert([] {
    for (size_t i = 1; i < std::size(statusErrors); ++i)
        if (statusErrors[i - 1].status >= statusErrors[i].status)
            return false;
    return true;
}());



uint32_t RtlNtStatusToDosError(uint32_t status) {
    if (status == 0 || (status & 0x20000000) != 0)
        return status;

    if ((status >> 16) == 0x8007)
        return status & 0xFFFF;

    // remove the hresult nt facility bit before looking up the ntstatus
    if ((status & 0xF0000000) == 0xD0000000)
        status &= ~0x30000000u;

    const auto* entry
        = std::lower_bound(std::begin(statusErrors), std::end(statusErrors), status,
                           [](const StatusError& entry, uint32_t value) { return entry.status < value; });
    if (entry != std::end(statusErrors) && entry->status == status)
        return entry->error;

    if ((status >> 16) == 0xC001)
        return status & 0xFFFF;

    constexpr uint32_t errorMrMidNotFound = 317;
    return errorMrMidNotFound;
}


// fill complete guest words with the big endian pattern and leave trailing bytes untouched
// validate the entire written range before storing and allow empty fills without a destination
void RtlFillMemoryUlong(Memory& memory, GuestAddress destination, uint32_t length, uint32_t pattern) {
    const uint32_t byteCount = length & ~3u;
    if (!byteCount)
        return;

    if (!destination || !memory.IsAccessible(destination, byteCount, true))
        throw std::out_of_range("rtl fill destination is not in writable guest memory");

    auto* words = static_cast<be<uint32_t>*>(memory.Translate(destination, byteCount));
    std::fill_n(words, byteCount / sizeof(uint32_t), be<uint32_t>(pattern));
}

// count accessible guest characters with room for the terminator in the 16 bit maximum length
void RtlInitAnsiString(Memory& memory, Pointer<AnsiString> destination, Pointer<const char> source) {
    AnsiString string{};
    string.buffer = source.guest_address();
    if (source) {
        constexpr uint32_t maximumLength = UINT16_MAX - 1;
        uint32_t length = 0;
        while (length < maximumLength) {
            const uint64_t address = uint64_t(source.guest_address()) + length;
            if (address > UINT32_MAX)
                throw std::out_of_range("ansi string exceeds guest address space");
            if (*Pointer<const char>(memory, static_cast<GuestAddress>(address)) == '\0')
                break;
            ++length;
        }
        string.length = static_cast<uint16_t>(length);
        string.maximumLength = static_cast<uint16_t>(length + 1);
    }
    *destination = string;
}

void RtlInitializeCriticalSection(Pointer<CriticalSection> section) {
    *section = MakeCriticalSection();
}

uint32_t RtlInitializeCriticalSectionAndSpinCount(Pointer<CriticalSection> section, uint32_t spinCount) {
    *section = MakeCriticalSection(spinCount);
    return 0;
}

void RtlEnterCriticalSection(Kernel& kernel, KThread& thread, PPCContext& cpu,
                             Pointer<CriticalSection> section) {
    if (!kernel.EnterCriticalSection(thread, section))
        cpu.Action = HostAction::Wait;
}

uint32_t RtlTryEnterCriticalSection(Kernel& kernel, KThread& thread, Pointer<CriticalSection> section) {
    return kernel.EnterCriticalSection(thread, section, true);
}

void RtlLeaveCriticalSection(Kernel& kernel, KThread& thread, Pointer<CriticalSection> section) {
    kernel.LeaveCriticalSection(thread, section);
}

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
    Bind<&RtlFillMemoryUlong>(XboxLibrary::XboxKrnl, "RtlFillMemoryUlong"),
    Bind<&RtlInitAnsiString>(XboxLibrary::XboxKrnl, "RtlInitAnsiString"),
    Bind<&RtlEnterCriticalSection>(XboxLibrary::XboxKrnl, "RtlEnterCriticalSection"),
    Bind<&RtlTryEnterCriticalSection>(XboxLibrary::XboxKrnl, "RtlTryEnterCriticalSection"),
    Bind<&RtlLeaveCriticalSection>(XboxLibrary::XboxKrnl, "RtlLeaveCriticalSection"),
    Bind<&RtlInitializeCriticalSection>(XboxLibrary::XboxKrnl, "RtlInitializeCriticalSection"),
    Bind<&RtlInitializeCriticalSectionAndSpinCount>(XboxLibrary::XboxKrnl,
                                                    "RtlInitializeCriticalSectionAndSpinCount"),
    Bind<&RtlImageXexHeaderField>(XboxLibrary::XboxKrnl, "RtlImageXexHeaderField"),
    Bind<&RtlNtStatusToDosError>(XboxLibrary::XboxKrnl, "RtlNtStatusToDosError"),
};


std::span<const Export> RtlExports() {
    return exports;
}

}  // namespace hle::krnl
