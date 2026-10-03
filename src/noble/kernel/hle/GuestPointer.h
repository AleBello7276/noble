#pragma once

#include "emulator/Memory.h"
#include "kernel/GuestPointer.h"
#include <stdexcept>
#include <type_traits>

namespace hle {

// report invalid memory through pointer validity when an api must return its own status code
enum class PointerValidation { Throw, Report };

// translate one typed guest argument and retain its guest address for the duration of a shim call
// use pointer<const t> for inputs and pointer<t> for output or input and output objects
// scalar guest memory should use be<t> and structure fields must describe the guest byte layout
// validation covers one object and get must not be indexed as an unchecked array
template <typename T, PointerValidation Validation = PointerValidation::Throw>
class Pointer {
    static_assert(!std::is_void_v<T>);

public:
    using element_type = T;

    // validate the whole object with read access for const types and write access for mutable types
    Pointer(Memory& memory, GuestAddress address) : address_(address) {
        static_assert(std::is_trivially_copyable_v<T>);
        if (!address)
            return;
        if (address % alignof(T) || !memory.IsAccessible(address, sizeof(T), !std::is_const_v<T>)) {
            if constexpr (Validation == PointerValidation::Throw)
                throw std::out_of_range("hle pointer is not in accessible aligned guest memory");
            return;
        }
        pointer_ = static_cast<T*>(memory.Translate(address, sizeof(T)));
    }

    // get the address supplied by guest code
    GuestAddress guest_address() const noexcept { return address_; }
    // get the translated host pointer or null for a null or reported invalid guest argument
    T* get() const noexcept { return pointer_; }
    // test whether the argument has an accessible translated object
    explicit operator bool() const noexcept { return pointer_ != nullptr; }
    // access the translated object or report a null guest pointer
    T& operator*() const {
        if (!pointer_)
            throw std::invalid_argument("hle pointer is null or inaccessible");
        return *pointer_;
    }
    // access a field of the translated object
    T* operator->() const { return &**this; }

private:
    GuestAddress address_;
    T* pointer_ = nullptr;
};

namespace detail {
template <typename T>
inline constexpr bool IsPointer = false;
template <typename T, PointerValidation Validation>
inline constexpr bool IsPointer<Pointer<T, Validation>> = true;
}  // namespace detail

}  // namespace hle
