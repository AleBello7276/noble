#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <type_traits>

// store a scalar in big endian byte order and convert automatically on reads and writes
template <typename T>
class be {
    static_assert((std::is_integral_v<T> || std::is_floating_point_v<T> || std::is_enum_v<T>)
                  && !std::is_same_v<T, bool>);
    static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8);
    static_assert(std::endian::native == std::endian::little || std::endian::native == std::endian::big);

public:
    constexpr be() = default;
    // encode a host value for storage in guest memory
    constexpr be(T value) noexcept { *this = value; }

    // decode the stored bytes to a host scalar without requiring alignment
    constexpr operator T() const noexcept { return std::bit_cast<T>(Swap(bytes_)); }

    // replace the guest bytes with an encoded host value
    constexpr be& operator=(T value) noexcept {
        bytes_ = Swap(std::bit_cast<Bytes>(value));
        return *this;
    }

    // update the decoded value and encode the result
    constexpr be& operator+=(T value) noexcept { return *this = static_cast<T>(T(*this) + value); }
    // subtract from the decoded value and encode the result
    constexpr be& operator-=(T value) noexcept { return *this = static_cast<T>(T(*this) - value); }
    // apply a bit mask to the decoded value
    constexpr be& operator&=(T value) noexcept requires std::is_integral_v<T> {
        return *this = static_cast<T>(T(*this) & value);
    }
    // add bits to the decoded value
    constexpr be& operator|=(T value) noexcept requires std::is_integral_v<T> {
        return *this = static_cast<T>(T(*this) | value);
    }
    // toggle bits in the decoded value
    constexpr be& operator^=(T value) noexcept requires std::is_integral_v<T> {
        return *this = static_cast<T>(T(*this) ^ value);
    }
    // increment the decoded value and return this field
    constexpr be& operator++() noexcept { return *this += T(1); }
    // increment the decoded value and return its previous host value
    constexpr T operator++(int) noexcept {
        const T value = *this;
        ++*this;
        return value;
    }
    // decrement the decoded value and return this field
    constexpr be& operator--() noexcept { return *this -= T(1); }
    // decrement the decoded value and return its previous host value
    constexpr T operator--(int) noexcept {
        const T value = *this;
        --*this;
        return value;
    }

private:
    using Bytes = std::array<std::byte, sizeof(T)>;
    // reverse scalar bytes only when the host uses little endian storage
    static constexpr Bytes Swap(Bytes bytes) noexcept {
        if constexpr (std::endian::native == std::endian::little)
            for (size_t i = 0; i < sizeof(T) / 2; ++i) {
                const auto value = bytes[i];
                bytes[i] = bytes[sizeof(T) - 1 - i];
                bytes[sizeof(T) - 1 - i] = value;
            }
        return bytes;
    }
    Bytes bytes_{};
};

static_assert(sizeof(be<uint32_t>) == sizeof(uint32_t));
static_assert(alignof(be<uint64_t>) == 1);
static_assert(std::is_trivially_copyable_v<be<uint32_t>>);
static_assert(std::is_standard_layout_v<be<uint32_t>>);
