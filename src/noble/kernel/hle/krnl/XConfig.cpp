#include "XConfig.h"

#include "Logger.h"
#include "kernel/Kernel.h"
#include <array>
#include <cstring>
#include <optional>

namespace hle::krnl {
namespace {

constexpr uint32_t statusSuccess = 0;
constexpr uint32_t statusAccessViolation = 0xC0000005;
constexpr uint32_t statusBufferTooSmall = 0xC0000023;
constexpr uint32_t statusObjectNameNotFound = 0xC0000034;
constexpr uint32_t statusInvalidParameter3 = 0xC00000F1;

enum class Category : uint16_t { Secured = 2, User = 3, Console = 7 };

// setting data is already encoded in guest byte order before it reaches the output buffer
struct Setting {
    std::array<uint8_t, 256> bytes{};
    uint16_t size;
};

template <typename T>
Setting Scalar(T value) {
    Setting result{{}, sizeof(T)};
    const be<T> encoded(value);
    std::memcpy(result.bytes.data(), &encoded, sizeof(encoded));
    return result;
}

std::optional<Setting> ReadSetting(Kernel& kernel, uint16_t category, uint16_t setting) {
    if (category == uint16_t(Category::Secured)) {
        switch (setting) {
        case 2:
            return Scalar<uint32_t>(0x00400100);  // ntsc m av region
        case 3:
            return Scalar<uint16_t>(0x00FF);  // north america game region
        case 4:
            return Scalar<uint32_t>(1);  // dvd region
        case 5:
        case 6:
            return Scalar<uint32_t>(0);  // reset key and system flags
        case 7:
        case 9:
            return Setting{{}, 2};  // power mode and vcs control
        case 8:
            return Setting{{}, 4};  // offline network id
        default:
            return std::nullopt;
        }
    }

    if (category == uint16_t(Category::User)) {
        if (!setting || setting > 0x30)
            return std::nullopt;
        const auto mode = kernel.GPU().GetDisplayMode();
        switch (setting) {
        case 2:
            return Setting{{'G', 'M', 'T', 0}, 4};  // utc standard timezone name
        case 8:
        case 0x11:
            return Scalar<uint64_t>(0);  // no default profile or live account
        case 9:
            return Scalar<uint32_t>(1);  // english
        case 0xA:
            return Scalar<uint32_t>(mode.widescreen ? 0x10000 : 0);
        case 0xB:
            return Scalar<uint32_t>(0x10001);  // dolby digital and pro logic
        case 0xC:
            return Scalar<uint32_t>(0x40);  // dashboard initialized
        case 0xE:
            return Scalar<uint8_t>(103);  // united states
        case 0xF:
            return Scalar<uint8_t>(3);  // online access and account creation allowed
        case 0x10:
            return Setting{{}, 256};  // empty smb configuration
        case 0x12:
            return Setting{{}, 16};  // empty live credentials
        case 0x13:
        case 0x14:
        case 0x15:
            return Scalar<uint32_t>((mode.width << 16) | (mode.height & 0xFFFF));
        case 0x16:
        case 0x18:
        case 0x23:
        case 0x25:
        case 0x27:
            return Scalar<uint32_t>(0xFF);  // no parental content restrictions
        case 0x1B:
        case 0x2A:
        case 0x2E:
        case 0x2F:
        case 0x30:
            return Scalar<uint8_t>(0);
        case 0x1C:
        case 0x1D:
            return Setting{{}, 32};  // empty parental hint answer and override
        case 0x1F:
            return Scalar<float>(0.7f);  // music volume
        case 0x22:
            return Scalar<uint32_t>(1);  // parental control version
        case 0x29:
            return Scalar<uint32_t>(0x300);  // normal video black level
        default:
            return Scalar<uint32_t>(0);  // remaining user flags and utc timezone values
        }
    }

    if (category == uint16_t(Category::Console)) {
        switch (setting) {
        case 1:
            return Scalar<uint16_t>(0x1000);  // screen saver disabled
        case 2:
        case 6:
            return Scalar<uint16_t>(0);  // auto shutdown and auto launch disable flags
        case 3:
            return Setting{{}, 256};  // empty wireless configuration
        case 4:
            return Scalar<uint32_t>(1);  // automatic camera settings
        case 5:
            return Setting{{}, 20};  // no play timer restriction
        case 7:
            return Scalar<uint16_t>(1);  // english qwerty keyboard
        default:
            return std::nullopt;
        }
    }
    return std::nullopt;
}

}  // namespace

// read a setting or query its size with a null buffer and a zero buffer size
// required size is a big endian word and is zero on failure as in xenia
uint32_t ExGetXConfigSetting(Kernel& kernel, Memory& memory, uint16_t category, uint16_t setting,
                             Pointer<uint8_t, PointerValidation::Report> buffer, uint16_t bufferSize,
                             Pointer<be<uint16_t>, PointerValidation::Report> requiredSize) {
    if (requiredSize.guest_address() && !requiredSize)
        return statusAccessViolation;

    if (requiredSize)
        *requiredSize = 0;

    if (!buffer.guest_address() && bufferSize)
        return statusInvalidParameter3;

    const auto value = ReadSetting(kernel, category, setting);

    if (!value) {
        LOG_WARN("Unknown xconfig setting category 0x{:04X} setting 0x{:04X}", category, setting);
        return statusObjectNameNotFound;
    }

    if (buffer.guest_address()) {
        if (bufferSize < value->size)
            return statusBufferTooSmall;
        if (!buffer || !memory.IsAccessible(buffer.guest_address(), value->size, true))
            return statusAccessViolation;
        std::memcpy(buffer.get(), value->bytes.data(), value->size);
    }

    if (requiredSize)
        *requiredSize = value->size;

    return statusSuccess;
}

constexpr std::array exports{Bind<&ExGetXConfigSetting>(XboxLibrary::XboxKrnl, "ExGetXConfigSetting")};

std::span<const Export> XConfigExports() {
    return exports;
}

}  // namespace hle::krnl
