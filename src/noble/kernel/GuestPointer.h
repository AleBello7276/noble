#pragma once

#include "core/endian.h"
#include <cstdint>

// store a typed 32 bit guest address inside a guest structure without embedding a host pointer
template <typename T>
class GuestPointer {
public:
    using element_type = T;

    constexpr GuestPointer() = default;
    // encode a guest address for storage in a pointer field
    constexpr GuestPointer(uint32_t address) noexcept : address_(address) {}
    // decode the guest address for host memory translation
    constexpr uint32_t guest_address() const noexcept { return address_; }
    // read the pointer field as a guest address
    constexpr operator uint32_t() const noexcept { return guest_address(); }
    // test whether the pointer field contains a nonzero address
    constexpr explicit operator bool() const noexcept { return guest_address() != 0; }
    // replace the encoded guest address
    constexpr GuestPointer& operator=(uint32_t address) noexcept {
        address_ = address;
        return *this;
    }

private:
    be<uint32_t> address_;
};

static_assert(sizeof(GuestPointer<void>) == 4);
static_assert(alignof(GuestPointer<void>) == 1);
static_assert(std::is_trivially_copyable_v<GuestPointer<void>>);
