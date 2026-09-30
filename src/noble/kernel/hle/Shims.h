#pragma once

#include "Loader/ImageLoader.h"
#include "cpu/PpcContext.h"
#include "emulator/Memory.h"
#include <bit>
#include <shared_mutex>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <unordered_map>

class Kernel;

namespace hle {

// expose guest state and checked memory access to one host kernel call
class Context {
public:
    Context(PPCContext& cpu_, Kernel& kernel_, Memory& memory_)
        : cpu(cpu_), kernel(kernel_), memory(memory_) {}

    // read a scalar integer argument from r3 through r10 or its big endian stack slot
    template <typename T>
    T Argument(size_t index) const {
        static_assert(std::is_integral_v<T> && sizeof(T) <= 8,
                      "hle integer arguments must fit in a guest register");

        if (index < 8)
            return static_cast<T>(cpu.GPRs[3 + index].u64);

        if (index - 8 > (UINT32_MAX - 0x54) / 8)
            throw std::out_of_range("hle argument index");

        const uint64_t address = uint64_t(uint32_t(cpu.GPRs[1].u64)) + 0x54 + (index - 8) * 8;

        if (address > UINT32_MAX)
            throw std::out_of_range("hle stack argument address");

        if constexpr (sizeof(T) <= 4)
            return static_cast<T>(ReadU32(static_cast<uint32_t>(address)));
        else
            return static_cast<T>(ReadU64(static_cast<uint32_t>(address)));
    }

    struct ArgumentCursor {
        size_t ordinal = 0;
        size_t floating = 1;
    };

    // consume a typed argument in declaration order for a generated shim
    template <typename T>
    T NextArgument(ArgumentCursor& cursor) const {
        const size_t ordinal = cursor.ordinal++;

        if constexpr (std::is_floating_point_v<T>) {
            static_assert(std::is_same_v<T, float> || std::is_same_v<T, double>);
            const size_t MAX_FLOAT_REG = 13;

            if (cursor.floating > MAX_FLOAT_REG)
                throw std::out_of_range("stack floating point hle arguments are not supported");

            return static_cast<T>(cpu.FPRs[cursor.floating++].f64);
        } else {
            return Argument<T>(ordinal);
        }
    }

    // store an integer result in r3 or a floating point result in f1
    template <typename T>
    void Return(T value) {
        if constexpr (std::is_floating_point_v<T>) {
            cpu.FPRs[1].f64 = value;
        } else {
            static_assert(std::is_integral_v<T> && sizeof(T) <= 8);

            if constexpr (sizeof(T) <= 4)
                cpu.GPRs[3].s64 = std::bit_cast<int32_t>(static_cast<uint32_t>(value));
            else
                cpu.GPRs[3].u64 = static_cast<uint64_t>(value);
        }
    }

    // translate a nonnull guest pointer after validating the entire requested range
    void* Translate(GuestAddress address, size_t size) const;

    // read a big endian guest word without requiring host alignment
    uint32_t ReadU32(GuestAddress address) const;

    // read a big endian guest doubleword without requiring host alignment
    uint64_t ReadU64(GuestAddress address) const;

    // write a big endian guest word
    void WriteU32(GuestAddress address, uint32_t value) const;

    // write a big endian guest doubleword
    void WriteU64(GuestAddress address, uint64_t value) const;

    PPCContext& cpu;
    Kernel& kernel;
    Memory& memory;
};

namespace detail {

// inject host services by reference without consuming a guest argument slot
template <typename T>
T Parameter(Context& context, Context::ArgumentCursor& cursor) {
    using Service = std::remove_cvref_t<T>;

    if constexpr (std::is_lvalue_reference_v<T> && std::is_same_v<Service, Context>) {
        return context;
    } else if constexpr (std::is_lvalue_reference_v<T> && std::is_same_v<Service, Kernel>) {
        return context.kernel;
    } else if constexpr (std::is_lvalue_reference_v<T> && std::is_same_v<Service, KThread>) {
        if (!context.cpu.HostThread)
            throw std::runtime_error("hle call has no guest thread");

        return *context.cpu.HostThread;
    } else if constexpr (std::is_lvalue_reference_v<T> && std::is_same_v<Service, Memory>) {
        return context.memory;
    } else if constexpr (std::is_lvalue_reference_v<T> && std::is_same_v<Service, PPCContext>) {
        return context.cpu;
    } else {
        static_assert(!std::is_reference_v<T> && !std::is_pointer_v<T>,
                      "hle guest parameters must be scalar values and host services must be references");
        return context.NextArgument<T>(cursor);
    }
}

template <auto Function>
struct Shim;

template <typename R, typename... Args, R (*Function)(Args...)>
struct Shim<Function> {
    static void Invoke(Context& context) {
        Context::ArgumentCursor cursor;
        // braced initialization extracts guest arguments in declaration order
        std::tuple<Args...> arguments{Parameter<Args>(context, cursor)...};

        if constexpr (std::is_void_v<R>)
            std::apply(Function, arguments);
        else
            context.Return(std::apply(Function, arguments));
    }
};

}  // namespace detail

// own host implementations separately from the immutable export name catalogue
class Registry {
public:
    using Handler = void (*)(Context&);
    using EntryPoint = void (*)(Registry*, PPCContext*, uint32_t, uint32_t, uint32_t) noexcept;

    Registry(Kernel& kernel, Memory& memory) : kernel_(kernel), memory_(memory) {}

    // bind an ordinary function with scalar guest arguments and optional host service references
    template <auto Function>
    void Register(XboxLibrary library, uint16_t ordinal) {
        RegisterShim<&detail::Shim<Function>::Invoke>(library, ordinal);
    }

    // bind a manual shim at compile time for signatures that need explicit abi handling
    template <Handler Function>
    void RegisterShim(XboxLibrary library, uint16_t ordinal) {
        static_assert(Function != nullptr);
        RegisterEntry(library, ordinal, &Invoke<Function>);
    }

    // resolve once when compiling a wrapper and retain that entry point for its lifetime
    EntryPoint Resolve(XboxLibrary library, uint16_t ordinal) const;

private:
    // call the selected shim directly and contain exceptions before returning to jit code
    template <Handler Function>
    static void Invoke(Registry* registry, PPCContext* cpu, uint32_t library, uint32_t ordinal,
                       uint32_t thunkAddress) noexcept {
        if (cpu->Fault != PPCFault::None)
            return;

        cpu->CIA = thunkAddress;

        try {
            Context context(*cpu, registry->kernel_, registry->memory_);
            Function(context);

            if (cpu->Fault == PPCFault::None)
                cpu->CIA = static_cast<uint32_t>(cpu->SPRs.LR) & ~uint32_t(3);
        } catch (const std::exception& error) {
            ReportFailure(*cpu, ordinal, thunkAddress, error.what());
        } catch (...) {
            ReportFailure(*cpu, ordinal, thunkAddress, "unknown host exception");
        }
    }

    // update registrations for future bindings without changing existing compiled wrappers
    void RegisterEntry(XboxLibrary library, uint16_t ordinal, EntryPoint entry);

    // supply a fixed fault target for imports missing an implementation at binding time
    static void MissingImport(Registry*, PPCContext*, uint32_t library, uint32_t ordinal,
                              uint32_t thunkAddress) noexcept;

    // record a host failure without unwinding through generated code
    static void ReportFailure(PPCContext& cpu, uint32_t ordinal, uint32_t thunkAddress,
                              const char* message) noexcept;

    // combine the library and ordinal into a unique registration key
    static uint64_t Key(XboxLibrary library, uint16_t ordinal) { return (uint64_t(library) << 32) | ordinal; }

    Kernel& kernel_;
    Memory& memory_;
    mutable std::shared_mutex mutex_;
    std::unordered_map<uint64_t, EntryPoint> entries_;
};

}  // namespace hle
