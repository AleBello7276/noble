#pragma once

#include <bit>

// possbile should check for endiannes of the platform for some situations
template <typename T>
constexpr T byte_swap(T x) noexcept {
    return std::byteswap(x);
}
