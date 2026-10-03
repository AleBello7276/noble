#include "Rtl.h"

#include "core/byte_swap.h"
#include <array>
#include <cstring>

namespace hle::krnl {
namespace {

// find an optional xex header field and return its inline value or guest storage address
uint32_t RtlImageXexHeaderField(Context& context, uint32_t headerAddress, uint32_t field) {
    if (!headerAddress)
        return 0;

    constexpr uint32_t fixedHeaderSize = 0x18;
    context.Translate(headerAddress, fixedHeaderSize);
    if (context.ReadU32(headerAddress) != 0x58455832)
        throw std::invalid_argument("invalid xex header magic");

    const uint32_t headerSize = context.ReadU32(headerAddress + 8);
    const uint32_t count = context.ReadU32(headerAddress + 0x14);
    const uint64_t tableEnd = fixedHeaderSize + uint64_t(count) * 8;
    if (tableEnd > headerSize)
        throw std::out_of_range("xex optional header table exceeds header bounds");

    const auto* bytes = static_cast<const uint8_t*>(context.Translate(headerAddress, headerSize));
    const auto readWord = [&](uint32_t offset) {
        uint32_t value;
        std::memcpy(&value, bytes + offset, sizeof(value));
        return byte_swap(value);
    };

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
    Bind<&RtlImageXexHeaderField>(XboxLibrary::XboxKrnl, "RtlImageXexHeaderField"),
};

}  // namespace

std::span<const Export> RtlExports() {
    return exports;
}

}  // namespace hle::krnl
