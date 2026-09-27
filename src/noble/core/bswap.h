#pragma once

#include <cstdint>

constexpr std::uint16_t bswap16(std::uint16_t x) noexcept {
    return static_cast<std::uint16_t>((x >> 8) | (x << 8));
}

constexpr std::uint32_t bswap32(std::uint32_t x) noexcept {
    return ((x & 0x000000FFu) << 24) | ((x & 0x0000FF00u) << 8) | ((x & 0x00FF0000u) >> 8)
           | ((x & 0xFF000000u) >> 24);
}

constexpr std::uint64_t bswap64(std::uint64_t x) noexcept {
    return ((x & 0x00000000000000FFull) << 56) | ((x & 0x000000000000FF00ull) << 40)
           | ((x & 0x0000000000FF0000ull) << 24) | ((x & 0x00000000FF000000ull) << 8)
           | ((x & 0x000000FF00000000ull) >> 8) | ((x & 0x0000FF0000000000ull) >> 24)
           | ((x & 0x00FF000000000000ull) >> 40) | ((x & 0xFF00000000000000ull) >> 56);
}
