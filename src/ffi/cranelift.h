#pragma once

#include "cranelift_ffi.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace cranelift {

using Type = ClType;
// an invalid entity can initialize or compare against any entity type
struct InvalidId {};
inline constexpr InvalidId INVALID_ID{};

// entity ids belong to their function or module and are distinct from integer constants
// default construction produces an invalid id and raw exposes the id for the c ffi
template <class Tag>
class EntityId {
public:
    // create an invalid id
    constexpr EntityId() noexcept = default;
    // accept the shared invalid sentinel without accepting integers implicitly
    constexpr EntityId(InvalidId) noexcept {}
    // explicitly wrap a raw c ffi id of this entity kind
    explicit constexpr EntityId(std::uint32_t id) noexcept : id_(id) {}

    // return the integer id for calls through the c ffi
    constexpr std::uint32_t raw() const noexcept { return id_; }
    // check the invalid sentinel without checking ownership or entity lifetime
    constexpr bool valid() const noexcept { return id_ != CL_INVALID_ID; }
    // compare ids of the same entity kind
    friend constexpr bool operator==(EntityId, EntityId) noexcept = default;

private:
    std::uint32_t id_ = CL_INVALID_ID;
};

using Value = EntityId<struct ValueTag>;
using Block = EntityId<struct BlockTag>;
using Inst = EntityId<struct InstTag>;
using Variable = EntityId<struct VariableTag>;
using FuncRef = EntityId<struct FuncRefTag>;
using FuncId = EntityId<struct FuncIdTag>;
using DataId = EntityId<struct DataIdTag>;
using GlobalValue = EntityId<struct GlobalValueTag>;
using SigRef = EntityId<struct SigRefTag>;
using MemFlags = ClMemFlags;
using Linkage = ClLinkage;
using IntCC = ClIntCC;
using FloatCC = ClFloatCC;
using Endianness = ClEndianness;
using AtomicRmwOp = ClAtomicRmwOp;
using CallConv = ClCallConv;
using TrapCode = ClTrapCode;

namespace detail {

// copy ids to c storage without treating wrapper objects as integer arrays
class RawValues {
public:
    explicit RawValues(std::span<const Value> values) : size_(values.size()) {
        if (size_ > local_.size())
            large_.resize(size_);
        auto* target = size_ > local_.size() ? large_.data() : local_.data();
        for (std::size_t i = 0; i < size_; ++i)
            target[i] = values[i].raw();
    }
    const ClValue* data() const noexcept { return size_ > local_.size() ? large_.data() : local_.data(); }
    std::size_t size() const noexcept { return size_; }

private:
    std::array<ClValue, 8> local_{};
    std::vector<ClValue> large_;
    std::size_t size_;
};

}  // namespace detail

namespace trapcodes {

inline constexpr TrapCode STACK_OVERFLOW = CL_TRAP_STACK_OVERFLOW;
inline constexpr TrapCode INTEGER_OVERFLOW = CL_TRAP_INTEGER_OVERFLOW;
inline constexpr TrapCode HEAP_OUT_OF_BOUNDS = CL_TRAP_HEAP_OUT_OF_BOUNDS;
inline constexpr TrapCode INTEGER_DIVISION_BY_ZERO = CL_TRAP_INTEGER_DIVISION_BY_ZERO;
inline constexpr TrapCode BAD_CONVERSION_TO_INTEGER = CL_TRAP_BAD_CONVERSION_TO_INTEGER;

// construct a user code from 1 through 250 or return zero and set the thread error
inline TrapCode user(std::uint8_t code) noexcept {
    return cl_trapcode_user(code);
}

}  // namespace trapcodes

// return the most recent error for this thread which is not cleared after success
inline const char* last_error() noexcept {
    return cl_last_error();
}

class JITModule;

namespace types {

// invalid type returned by a failed type constructor
inline constexpr Type INVALID = 0;

// return the cranelift i8 type
inline Type I8() noexcept {
    return cl_type_i8();
}

// return the cranelift i16 type
inline Type I16() noexcept {
    return cl_type_i16();
}

// return the cranelift i32 type
inline Type I32() noexcept {
    return cl_type_i32();
}

// return the cranelift i64 type
inline Type I64() noexcept {
    return cl_type_i64();
}

// return the cranelift i128 type
inline Type I128() noexcept {
    return cl_type_i128();
}

// return the cranelift f16 type with limited backend support
inline Type F16() noexcept {
    return cl_type_f16();
}

// return the cranelift f32 type
inline Type F32() noexcept {
    return cl_type_f32();
}

// return the cranelift f64 type
inline Type F64() noexcept {
    return cl_type_f64();
}

// return the cranelift f128 type with limited backend support
inline Type F128() noexcept {
    return cl_type_f128();
}

// return i8 i16 i32 i64 or i128 for the requested bit width
// return INVALID and set last_error for an unsupported width
inline Type Integer(std::uint16_t bits) noexcept {
    return cl_type_int(bits);
}

// create a fixed vector such as Vector(I8(), 16) for i8x16
// lanes must be a supported power of two and the result must fit the type encoding
inline Type Vector(Type lane_type, std::uint32_t lanes) noexcept {
    return cl_type_vector(lane_type, lanes);
}

// convert a fixed vector of at most 256 bits to a dynamically scaled vector
inline Type DynamicVector(Type fixed_vector_type) noexcept {
    return cl_type_vector_to_dynamic(fixed_vector_type);
}

// return the target pointer width as an integer type such as i32 or i64
Type Pointer(const JITModule& module) noexcept;

}  // namespace types

namespace detail {
template <typename T, void (*Drop)(T*)>
class Owned {
public:
    // take ownership of a raw pointer
    explicit Owned(T* ptr = nullptr) noexcept : ptr_(ptr) {}

    // release the owned pointer through its drop function
    ~Owned() { reset(); }

    // prevent two owners from holding the same pointer
    Owned(const Owned&) = delete;

    // prevent copying an owned pointer
    Owned& operator=(const Owned&) = delete;

    // transfer ownership from another owner
    Owned(Owned&& other) noexcept : ptr_(other.release()) {}

    // replace this pointer with one transferred from another owner
    Owned& operator=(Owned&& other) noexcept {
        if (this != &other)
            reset(other.release());
        return *this;
    }

    // return the pointer without transferring ownership
    T* get() const noexcept { return ptr_; }

    // return the pointer and relinquish ownership
    T* release() noexcept { return std::exchange(ptr_, nullptr); }

    // drop the current pointer and take ownership of a replacement
    void reset(T* ptr = nullptr) noexcept {
        if (ptr_)
            Drop(ptr_);
        ptr_ = ptr;
    }

    // report whether this owner holds a pointer
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

private:
    T* ptr_;
};
}  // namespace detail

class SettingsBuilder {
public:
    // create a cranelift settings builder
    SettingsBuilder() noexcept : handle_(cl_settings_builder_new()) {}

    // take ownership of an existing settings builder
    explicit SettingsBuilder(ClSettingsBuilder* raw) noexcept : handle_(raw) {}

    // return the underlying settings builder without transferring ownership
    ClSettingsBuilder* raw() const noexcept { return handle_.get(); }

    // transfer ownership of the settings builder to the caller
    ClSettingsBuilder* release() noexcept { return handle_.release(); }

    // report whether the settings builder was created successfully
    explicit operator bool() const noexcept { return static_cast<bool>(handle_); }

    // set a named cranelift setting to a string value
    bool set(const char* name, const char* value) noexcept { return cl_settings_set(raw(), name, value); }

    // enable a named cranelift setting
    bool enable(const char* name) noexcept { return cl_settings_enable(raw(), name); }

private:
    detail::Owned<ClSettingsBuilder, cl_settings_builder_drop> handle_;
};

class Isa {
public:
    // take ownership of a compiled target isa
    explicit Isa(ClIsa* raw = nullptr) noexcept : handle_(raw) {}

    // return the underlying target isa without transferring ownership
    ClIsa* raw() const noexcept { return handle_.get(); }

    // transfer ownership of the target isa to the caller
    ClIsa* release() noexcept { return handle_.release(); }

    // report whether this object holds a target isa
    explicit operator bool() const noexcept { return static_cast<bool>(handle_); }

private:
    detail::Owned<ClIsa, cl_isa_drop> handle_;
};

class NativeBuilder {
public:
    // create a builder for the host target with inferred cpu flags
    NativeBuilder() noexcept : handle_(cl_native_builder()) {}

    // create a builder for the host target with optional cpu flag inference
    explicit NativeBuilder(bool infer_native_flags) noexcept
        : handle_(cl_native_builder_with_options(infer_native_flags)) {}

    // return the underlying native builder without transferring ownership
    ClNativeBuilder* raw() const noexcept { return handle_.get(); }

    // report whether the native builder was created successfully
    explicit operator bool() const noexcept { return static_cast<bool>(handle_); }

    // set a named target setting to a string value
    bool set(const char* name, const char* value) noexcept { return cl_native_set(raw(), name, value); }

    // enable a named target setting
    bool enable(const char* name) noexcept { return cl_native_enable(raw(), name); }

    // consume this builder and the settings builder to create a target isa
    Isa finish(SettingsBuilder&& flags) noexcept {
        return Isa(cl_isa_finish(handle_.release(), flags.release()));
    }

private:
    detail::Owned<ClNativeBuilder, cl_native_builder_drop> handle_;
};

// select a module wide optimization level before creating the jit module
enum class OptimizationLevel { None, Speed, SpeedAndSize };

// pass any supported cranelift setting by its original name and value
struct Setting {
    const char* name;
    const char* value;
};

class JITBuilder {
public:
    // create a host jit builder with default settings including opt_level none
    JITBuilder() noexcept : handle_(cl_jit_builder_new()) {}

    // take ownership of an existing jit builder
    explicit JITBuilder(ClJitBuilder* raw) noexcept : handle_(raw) {}

    // create a host jit builder with named settings
    static JITBuilder with_flags(const char* const* names, const char* const* values,
                                 std::size_t count) noexcept {
        return JITBuilder(cl_jit_builder_with_flags(names, values, count));
    }

    // create a host jit builder from named settings such as {{"opt_level", "speed"}}
    // invalid names or values return an empty builder and set last_error
    static JITBuilder with_flags(std::initializer_list<Setting> settings) {
        std::vector<const char*> names;
        std::vector<const char*> values;
        names.reserve(settings.size());
        values.reserve(settings.size());
        for (const Setting& setting : settings) {
            names.push_back(setting.name);
            values.push_back(setting.value);
        }
        return with_flags(names.data(), values.data(), names.size());
    }

    // none minimizes compile time and is the default
    // speed favors execution speed
    // speed_and_size also reduces code size
    static JITBuilder with_optimization_level(OptimizationLevel level) noexcept {
        const char* value = "none";
        switch (level) {
        case OptimizationLevel::None:
            break;
        case OptimizationLevel::Speed:
            value = "speed";
            break;
        case OptimizationLevel::SpeedAndSize:
            value = "speed_and_size";
            break;
        }
        const char* name = "opt_level";
        return with_flags(&name, &value, 1);
    }

    // consume a target isa to create a jit builder
    static JITBuilder with_isa(Isa&& isa) noexcept {
        return JITBuilder(cl_jit_builder_with_isa(isa.release()));
    }

    // return the underlying jit builder without transferring ownership
    ClJitBuilder* raw() const noexcept { return handle_.get(); }

    // transfer ownership of the jit builder to the caller
    ClJitBuilder* release() noexcept { return handle_.release(); }

    // report whether the jit builder was created successfully
    explicit operator bool() const noexcept { return static_cast<bool>(handle_); }

    // register a host symbol for use by compiled code before moving this builder into a module
    bool symbol(const char* name, const void* address) noexcept {
        return cl_jit_builder_symbol(raw(), name, address);
    }

    // register a host function with the same name used by its imported declaration
    template <typename Function>
    requires std::is_function_v<Function> bool symbol(const char* name, Function* address) noexcept {
        return symbol(name, reinterpret_cast<const void*>(address));
    }

private:
    detail::Owned<ClJitBuilder, cl_jit_builder_drop> handle_;
};

class Signature {
public:
    // wrap a signature and optionally take ownership of it
    Signature(ClSignature* raw = nullptr, bool owned = false) noexcept : ptr_(raw), owned_(owned) {}

    // drop the signature only when this wrapper owns it
    ~Signature() { reset(); }

    // prevent copying a signature wrapper
    Signature(const Signature&) = delete;

    // prevent assigning a copied signature wrapper
    Signature& operator=(const Signature&) = delete;

    // transfer a signature wrapper and its ownership state
    Signature(Signature&& other) noexcept
        : ptr_(std::exchange(other.ptr_, nullptr)), owned_(std::exchange(other.owned_, false)) {}

    // replace this signature wrapper with a moved one
    Signature& operator=(Signature&& other) noexcept {
        if (this != &other) {
            reset();
            ptr_ = std::exchange(other.ptr_, nullptr);
            owned_ = std::exchange(other.owned_, false);
        }
        return *this;
    }

    // return the underlying signature without transferring ownership
    ClSignature* raw() const noexcept { return ptr_; }

    // report whether this wrapper holds a signature
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    // append a parameter type to the signature
    void push_param(Type type) noexcept { cl_signature_push_param(ptr_, type); }

    // append a return type to the signature
    void push_return(Type type) noexcept { cl_signature_push_return(ptr_, type); }

    // set the calling convention used by the signature
    bool set_call_conv(CallConv convention) noexcept {
        return cl_signature_set_call_conv(ptr_, static_cast<std::uint32_t>(convention));
    }

private:
    // drop an owned signature and clear this wrapper
    void reset() noexcept {
        if (owned_ && ptr_)
            cl_signature_drop(ptr_);
        ptr_ = nullptr;
        owned_ = false;
    }
    ClSignature* ptr_;
    bool owned_;
};

class Context {
public:
    // take ownership of a cranelift codegen context
    explicit Context(ClContext* raw = nullptr) noexcept : handle_(raw) {}

    // return the underlying codegen context without transferring ownership
    ClContext* raw() const noexcept { return handle_.get(); }

    // report whether this wrapper holds a codegen context
    explicit operator bool() const noexcept { return static_cast<bool>(handle_); }

    // return a signature borrowed from this context for editing before function building
    // keep this context alive while using the borrowed signature
    Signature signature() noexcept { return Signature(cl_context_signature(raw()), false); }

    // verify the function in this context against the module target
    bool verify(const JITModule& module) const noexcept;

    // return a snapshot of the current function ir for debugging before or after finish
    std::string ir() const {
        std::vector<ClIRComment> comments;
        for (const auto& comment : comments_)
            comments.push_back({comment.instruction.raw(), comment.block.raw(), comment.text.c_str()});

        const std::size_t required
            = cl_context_display_with_comments(raw(), comments.data(), comments.size(), nullptr, 0);

        if (required == 0)
            return {};

        std::string result(required, '\0');
        if (cl_context_display_with_comments(raw(), comments.data(), comments.size(), result.data(),
                                             result.size())
            == 0)
            return {};
        result.pop_back();
        return result;
    }

    // attach a display comment to an instruction in this function
    void comment(Inst instruction, std::string text) {
        comments_.push_back({instruction, INVALID_ID, std::move(text)});
    }
    void comment_at(Block block, Inst previous, std::string text) {
        comments_.push_back({previous, block, std::move(text)});
    }
    void clear_comments() noexcept { comments_.clear(); }

private:
    detail::Owned<ClContext, cl_context_drop> handle_;
    struct Comment {
        Inst instruction;
        Block block;
        std::string text;
    };
    std::vector<Comment> comments_;
};

class DataDescription {
public:
    // create an empty data description
    DataDescription() noexcept : handle_(cl_data_description_new()) {}

    // return the underlying data description without transferring ownership
    ClDataDescription* raw() const noexcept { return handle_.get(); }

    // report whether the data description was created successfully
    explicit operator bool() const noexcept { return static_cast<bool>(handle_); }

    // clear the initializer and relocations for reuse
    void clear() noexcept { cl_data_description_clear(raw()); }

    // define a zero initialized data object of the given size
    void define_zeroinit(std::size_t size) noexcept { cl_data_description_define_zeroinit(raw(), size); }

    // copy bytes into the data initializer
    bool define(std::span<const std::uint8_t> bytes) noexcept {
        return cl_data_description_define(raw(), bytes.data(), bytes.size());
    }

    // set the required power of two alignment
    bool set_align(std::uint64_t align) noexcept { return cl_data_description_set_align(raw(), align); }

private:
    detail::Owned<ClDataDescription, cl_data_description_drop> handle_;
};

class FunctionBuilderContext {
public:
    // create reusable state for building cranelift functions
    FunctionBuilderContext() noexcept : handle_(cl_function_builder_context_new()) {}

    // return the underlying builder context without transferring ownership
    ClFunctionBuilderContext* raw() const noexcept { return handle_.get(); }

    // report whether the builder context was created successfully
    explicit operator bool() const noexcept { return static_cast<bool>(handle_); }

private:
    detail::Owned<ClFunctionBuilderContext, cl_function_builder_context_drop> handle_;
};

// hold declarations and compiled code for all functions in this module
// there is no per function deletion or replacement in the pinned jit
class JITModule {
public:
    // consume a jit builder to create a module
    explicit JITModule(JITBuilder&& builder) noexcept : handle_(cl_jit_module_new(builder.release())) {}

    // take ownership of an existing jit module
    explicit JITModule(ClJitModule* raw) noexcept : handle_(raw) {}

    // return the underlying jit module without transferring ownership
    ClJitModule* raw() const noexcept { return handle_.get(); }

    // report whether this wrapper holds a jit module
    explicit operator bool() const noexcept { return static_cast<bool>(handle_); }

    // create a codegen context with the module calling convention
    Context make_context() const noexcept { return Context(cl_module_make_context(raw())); }

    // return the target pointer type
    Type pointer_type() const noexcept { return cl_module_pointer_type(raw()); }

    // clear a codegen context for another function after its builder has finished
    void clear_context(Context& context) const noexcept {
        cl_module_clear_context(raw(), context.raw());
        context.clear_comments();
    }

    // create a signature with the module calling convention
    Signature make_signature() const noexcept { return Signature(cl_module_make_signature(raw()), true); }

    // clear a signature for reuse with this module
    void clear_signature(Signature& signature) const noexcept {
        cl_module_clear_signature(raw(), signature.raw());
    }

    // declare a named function and return its module identifier
    FuncId declare_function(const char* name, Linkage linkage, const Signature& signature) noexcept {
        return FuncId{
            cl_module_declare_function(raw(), name, static_cast<std::uint32_t>(linkage), signature.raw())};
    }

    // import a declared function into the given codegen context
    FuncRef declare_func_in_func(FuncId id, Context& context) noexcept {
        return FuncRef{cl_module_declare_func_in_func(raw(), id.raw(), context.raw())};
    }

    // compile and define a declared function from the codegen context once per func id
    bool define_function(FuncId id, Context& context) noexcept {
        return cl_module_define_function(raw(), id.raw(), context.raw());
    }

    // declare a named data object and return its module identifier
    DataId declare_data(const char* name, Linkage linkage, bool writable, bool tls) noexcept {
        return DataId{
            cl_module_declare_data(raw(), name, static_cast<std::uint32_t>(linkage), writable, tls)};
    }

    // import a declared data object into the given codegen context
    GlobalValue declare_data_in_func(DataId id, Context& context) const noexcept {
        return GlobalValue{cl_module_declare_data_in_func(raw(), id.raw(), context.raw())};
    }

    // define a declared data object from its description
    bool define_data(DataId id, const DataDescription& description) noexcept {
        return cl_module_define_data(raw(), id.raw(), description.raw());
    }

    // apply relocations and make all pending definitions callable or readable
    bool finalize_definitions() noexcept { return cl_jit_module_finalize_definitions(raw()); }

    // return the address of a compiled and finalized function
    // calling before finalization or with an invalid id can abort in the rust layer
    const void* get_finalized_function(FuncId id) const noexcept {
        return cl_jit_module_get_finalized_function(raw(), id.raw());
    }

    // cast a finalized address to a function pointer whose signature and calling convention match
    // keep this module and any called host symbols alive while using the returned pointer
    template <typename Function>
    Function get_finalized_function_as(FuncId id) const noexcept {
        static_assert(std::is_pointer_v<Function> && std::is_function_v<std::remove_pointer_t<Function>>,
                      "Function must be a function pointer type");
        return reinterpret_cast<Function>(const_cast<void*>(get_finalized_function(id)));
    }

    // return the address and size of finalized data
    std::pair<const void*, std::size_t> get_finalized_data(DataId id) const noexcept {
        std::size_t size = 0;
        const void* address = cl_jit_module_get_finalized_data(raw(), id.raw(), &size);
        return {address, size};
    }

    // reclaim every code and data allocation after no compiled function can still run
    // consume the module and invalidate all finalized function and data pointers
    // dropping the module without this call intentionally leaves finalized memory allocated
    void free_memory() noexcept { cl_jit_module_free_memory(handle_.release()); }

private:
    detail::Owned<ClJitModule, cl_jit_module_drop> handle_;
};

// expose the pointer type beside the scalar and vector constructors
inline Type types::Pointer(const JITModule& module) noexcept {
    return module.pointer_type();
}

// verify this context against the module target
inline bool Context::verify(const JITModule& module) const noexcept {
    return cl_context_verify(raw(), module.raw());
}

class InstBuilder {
public:
    // terminate the current block with a native trap and the supplied reason code
    // a zero code returns INVALID_ID and executing the trap raises a native machine exception
    Inst trap(TrapCode code) const noexcept { return Inst{cl_ins_trap(builder_, code)}; }

    // trap when the scalar integer condition is zero and continue normally otherwise
    Inst trapz(Value condition, TrapCode code) const noexcept {
        return Inst{cl_ins_trapz(builder_, condition.raw(), code)};
    }

    // trap when the scalar integer condition is nonzero and continue normally otherwise
    Inst trapnz(Value condition, TrapCode code) const noexcept {
        return Inst{cl_ins_trapnz(builder_, condition.raw(), code)};
    }

    // emit a native debugger breakpoint without terminating the block
    Inst debugtrap() const noexcept { return Inst{cl_ins_debugtrap(builder_)}; }

    // add matching unsigned scalar integers and trap with the supplied reason if the sum overflows
    Value uadd_overflow_trap(Value left, Value right, TrapCode code) const noexcept {
        return Value{cl_ins_uadd_overflow_trap(builder_, left.raw(), right.raw(), code)};
    }

    // Emit a CLIR nop (does not generate a machine code nop).
    Inst nop() const noexcept { return Inst{cl_ins_nop(builder_)}; }

    // borrow the active function builder for instruction insertion
    explicit InstBuilder(ClFunctionBuilder* builder) noexcept : builder_(builder) {}

    // insert an integer constant with the given type
    Value iconst(Type type, std::int64_t immediate) const noexcept {
        return Value{cl_ins_iconst(builder_, type, immediate)};
    }

    // insert an f32 constant from its bit pattern
    Value f32const(std::uint32_t bits) const noexcept { return Value{cl_ins_f32const(builder_, bits)}; }

    // insert an f64 constant from its bit pattern
    Value f64const(std::uint64_t bits) const noexcept { return Value{cl_ins_f64const(builder_, bits)}; }

    // insert the address of an imported global value
    Value symbol_value(Type type, GlobalValue global) const noexcept {
        return Value{cl_ins_symbol_value(builder_, type, global.raw())};
    }

    // define direct wrappers for binary cranelift instructions
#define CL_BINARY_METHOD(name)                                                                               \
    Value name(Value left, Value right) const noexcept {                                                     \
        return Value{cl_ins_##name(builder_, left.raw(), right.raw())};                                      \
    }

    // add two integer values
    CL_BINARY_METHOD(iadd)

    // add equal-width scalar integers and return the sum followed by an i8 unsigned overflow flag
    std::pair<Value, Value> uadd_overflow(Value left, Value right) const noexcept {
        const auto result = cl_ins_uadd_overflow(builder_, left.raw(), right.raw());
        return {Value{result.first}, Value{result.second}};
    }

    // include an i8 carry input where nonzero means one and return the sum followed by an i8 overflow flag
    // the pinned x64 backend currently rejects this instruction during compilation
    std::pair<Value, Value> uadd_overflow_cin(Value left, Value right, Value carry_in) const noexcept {
        const auto result = cl_ins_uadd_overflow_cin(builder_, left.raw(), right.raw(), carry_in.raw());
        return {Value{result.first}, Value{result.second}};
    }

    // subtract the right integer from the left integer
    CL_BINARY_METHOD(isub)

    // multiply two integer values
    CL_BINARY_METHOD(imul)

    // choose the larger integer using unsigned comparison with matching operand types
    CL_BINARY_METHOD(umax)

    // choose the smaller integer using unsigned comparison with matching operand types
    CL_BINARY_METHOD(umin)

    // choose the larger integer using signed comparison with matching operand types
    CL_BINARY_METHOD(smax)

    // choose the smaller integer using signed comparison with matching operand types
    CL_BINARY_METHOD(smin)

    // compute the bitwise and of two integer values
    CL_BINARY_METHOD(band)

    // compute the bitwise or of two integer values
    CL_BINARY_METHOD(bor)

    // compute the bitwise xor of two integer values
    CL_BINARY_METHOD(bxor)

    // shift the left integer left by the right integer
    CL_BINARY_METHOD(ishl)

    // shift the left integer right without sign extension
    CL_BINARY_METHOD(ushr)

    // shift the left integer right with sign extension
    CL_BINARY_METHOD(sshr)

    // divide the left unsigned integer by the right integer
    CL_BINARY_METHOD(udiv)

    // divide the left signed integer by the right integer
    CL_BINARY_METHOD(sdiv)

    // compute the unsigned remainder of two integers
    CL_BINARY_METHOD(urem)

    // compute the signed remainder of two integers
    CL_BINARY_METHOD(srem)

    // rotate the left integer left by the right integer
    CL_BINARY_METHOD(rotl)

    // rotate the left integer right by the right integer
    CL_BINARY_METHOD(rotr)

    // add two floating point values
    CL_BINARY_METHOD(fadd)

    // subtract the right floating point value from the left
    CL_BINARY_METHOD(fsub)

    // multiply two floating point values
    CL_BINARY_METHOD(fmul)

    // divide the left floating point value by the right
    CL_BINARY_METHOD(fdiv)
#undef CL_BINARY_METHOD

    // reinterpret equal-sized types without numerical conversion using flags from this function
    // specify endianness when vector lane counts differ and use builder memflags_new for scalar casts
    Value bitcast(Type type, MemFlags flags, Value value) const noexcept {
        return Value{cl_ins_bitcast(builder_, type, flags, value.raw())};
    }

    // copy the sign bit of right onto left without changing the other bits
    Value fcopysign(Value left, Value right) const noexcept {
        return Value{cl_ins_fcopysign(builder_, left.raw(), right.raw())};
    }

    // choose the smaller float and propagate nan with negative zero preferred over positive zero
    Value fmin(Value left, Value right) const noexcept {
        return Value{cl_ins_fmin(builder_, left.raw(), right.raw())};
    }

    // choose the larger float and propagate nan with positive zero preferred over negative zero
    Value fmax(Value left, Value right) const noexcept {
        return Value{cl_ins_fmax(builder_, left.raw(), right.raw())};
    }

    // compute the floating point square root
    Value sqrt(Value value) const noexcept { return Value{cl_ins_sqrt(builder_, value.raw())}; }

    // flip the floating point sign bit without changing the other bits
    Value fneg(Value value) const noexcept { return Value{cl_ins_fneg(builder_, value.raw())}; }

    // clear the floating point sign bit without changing the other bits
    Value fabs(Value value) const noexcept { return Value{cl_ins_fabs(builder_, value.raw())}; }

    // round toward positive infinity and keep the floating point type
    Value ceil(Value value) const noexcept { return Value{cl_ins_ceil(builder_, value.raw())}; }

    // round toward negative infinity and keep the floating point type
    Value floor(Value value) const noexcept { return Value{cl_ins_floor(builder_, value.raw())}; }

    // round toward zero and keep the floating point type
    Value trunc(Value value) const noexcept { return Value{cl_ins_trunc(builder_, value.raw())}; }

    // round to the nearest integral float with ties to even
    Value nearest(Value value) const noexcept { return Value{cl_ins_nearest(builder_, value.raw())}; }

    // convert f64x2 to f32x4 with rounding to nearest ties to even and zero the upper two lanes
    Value fvdemote(Value value) const noexcept { return Value{cl_ins_fvdemote(builder_, value.raw())}; }

    // convert the lower two lanes of f32x4 to f64x2 and discard the upper lanes
    Value fvpromote_low(Value value) const noexcept {
        return Value{cl_ins_fvpromote_low(builder_, value.raw())};
    }

    // convert a scalar float to a wider float type preserving its numerical value
    Value fpromote(Type type, Value value) const noexcept {
        return Value{cl_ins_fpromote(builder_, type, value.raw())};
    }

    // convert a scalar float to a narrower float type with rounding to nearest ties to even
    Value fdemote(Type type, Value value) const noexcept {
        return Value{cl_ins_fdemote(builder_, type, value.raw())};
    }

    // convert a scalar float to an unsigned integer toward zero and trap on nan or overflow
    Value fcvt_to_uint(Type type, Value value) const noexcept {
        return Value{cl_ins_fcvt_to_uint(builder_, type, value.raw())};
    }

    // convert a scalar float to a signed integer toward zero and trap on nan or overflow
    Value fcvt_to_sint(Type type, Value value) const noexcept {
        return Value{cl_ins_fcvt_to_sint(builder_, type, value.raw())};
    }

    // convert float lanes to unsigned integers toward zero with clamping and nan converted to zero
    Value fcvt_to_uint_sat(Type type, Value value) const noexcept {
        return Value{cl_ins_fcvt_to_uint_sat(builder_, type, value.raw())};
    }

    // convert float lanes to signed integers toward zero with clamping and nan converted to zero
    Value fcvt_to_sint_sat(Type type, Value value) const noexcept {
        return Value{cl_ins_fcvt_to_sint_sat(builder_, type, value.raw())};
    }

    // convert unsigned integer lanes to floats with rounding to nearest ties to even
    Value fcvt_from_uint(Type type, Value value) const noexcept {
        return Value{cl_ins_fcvt_from_uint(builder_, type, value.raw())};
    }

    // convert signed integer lanes to floats with rounding to nearest ties to even
    Value fcvt_from_sint(Type type, Value value) const noexcept {
        return Value{cl_ins_fcvt_from_sint(builder_, type, value.raw())};
    }

    // compute left times right plus addend with a single rounding and matching float types
    Value fma(Value left, Value right, Value addend) const noexcept {
        return Value{cl_ins_fma(builder_, left.raw(), right.raw(), addend.raw())};
    }

    // compare matching float types with an i8 scalar result or a lane mask for vectors
    // not_equal includes nan while ordered_not_equal excludes nan
    Value fcmp(FloatCC condition, Value left, Value right) const noexcept {
        return Value{cl_ins_fcmp(builder_, static_cast<std::uint32_t>(condition), left.raw(), right.raw())};
    }

    // negate an integer value
    Value ineg(Value value) const noexcept { return Value{cl_ins_ineg(builder_, value.raw())}; }

    // invert every bit of an integer value
    Value bnot(Value value) const noexcept { return Value{cl_ins_bnot(builder_, value.raw())}; }

    // count leading zero bits with a result of the same type and the input bit width for zero
    Value clz(Value value) const noexcept { return Value{cl_ins_clz(builder_, value.raw())}; }

    // count trailing zero bits with a result of the same type and the input bit width for zero
    Value ctz(Value value) const noexcept { return Value{cl_ins_ctz(builder_, value.raw())}; }

    // count set bits with a result of the same type as the input
    Value popcnt(Value value) const noexcept { return Value{cl_ins_popcnt(builder_, value.raw())}; }

    // reverse the byte order of an integer value
    Value bswap(Value value) const noexcept { return Value{cl_ins_bswap(builder_, value.raw())}; }

    // narrow an integer value to the given type
    Value ireduce(Type type, Value value) const noexcept {
        return Value{cl_ins_ireduce(builder_, type, value.raw())};
    }

    // widen an integer value with zero extension
    Value uextend(Type type, Value value) const noexcept {
        return Value{cl_ins_uextend(builder_, type, value.raw())};
    }

    // widen an integer value with sign extension
    Value sextend(Type type, Value value) const noexcept {
        return Value{cl_ins_sextend(builder_, type, value.raw())};
    }

    // add a signed immediate to an integer value
    Value iadd_imm(Value value, std::int64_t immediate) const noexcept {
        return Value{cl_ins_iadd_imm(builder_, value.raw(), immediate)};
    }

    // add an immediate to an integer with a sign-extended immediate
    Value iadd_imm_s(Value value, std::int64_t immediate) const noexcept {
        return Value{cl_ins_iadd_imm_s(builder_, value.raw(), immediate)};
    }

    // add an immediate to an integer with a zero-extended immediate
    Value iadd_imm_u(Value value, std::uint64_t immediate) const noexcept {
        return Value{cl_ins_iadd_imm_u(builder_, value.raw(), immediate)};
    }

    // multiply an integer by an immediate with a sign-extended immediate
    Value imul_imm_s(Value value, std::int64_t immediate) const noexcept {
        return Value{cl_ins_imul_imm_s(builder_, value.raw(), immediate)};
    }

    // multiply an integer by an immediate with a zero-extended immediate
    Value imul_imm_u(Value value, std::uint64_t immediate) const noexcept {
        return Value{cl_ins_imul_imm_u(builder_, value.raw(), immediate)};
    }

    // divide an unsigned integer by an immediate with a sign-extended immediate
    Value udiv_imm_s(Value value, std::int64_t immediate) const noexcept {
        return Value{cl_ins_udiv_imm_s(builder_, value.raw(), immediate)};
    }

    // divide an unsigned integer by an immediate with a zero-extended immediate
    Value udiv_imm_u(Value value, std::uint64_t immediate) const noexcept {
        return Value{cl_ins_udiv_imm_u(builder_, value.raw(), immediate)};
    }

    // divide a signed integer by an immediate with a sign-extended immediate
    Value sdiv_imm_s(Value value, std::int64_t immediate) const noexcept {
        return Value{cl_ins_sdiv_imm_s(builder_, value.raw(), immediate)};
    }

    // divide a signed integer by an immediate with a zero-extended immediate
    Value sdiv_imm_u(Value value, std::uint64_t immediate) const noexcept {
        return Value{cl_ins_sdiv_imm_u(builder_, value.raw(), immediate)};
    }

    // compute the unsigned remainder with an immediate divisor with a sign-extended immediate
    Value urem_imm_s(Value value, std::int64_t immediate) const noexcept {
        return Value{cl_ins_urem_imm_s(builder_, value.raw(), immediate)};
    }

    // compute the unsigned remainder with an immediate divisor with a zero-extended immediate
    Value urem_imm_u(Value value, std::uint64_t immediate) const noexcept {
        return Value{cl_ins_urem_imm_u(builder_, value.raw(), immediate)};
    }

    // compute the signed remainder with an immediate divisor with a sign-extended immediate
    Value srem_imm_s(Value value, std::int64_t immediate) const noexcept {
        return Value{cl_ins_srem_imm_s(builder_, value.raw(), immediate)};
    }

    // compute the signed remainder with an immediate divisor with a zero-extended immediate
    Value srem_imm_u(Value value, std::uint64_t immediate) const noexcept {
        return Value{cl_ins_srem_imm_u(builder_, value.raw(), immediate)};
    }

    // apply bitwise and with an immediate with a sign-extended immediate
    Value band_imm_s(Value value, std::int64_t immediate) const noexcept {
        return Value{cl_ins_band_imm_s(builder_, value.raw(), immediate)};
    }

    // apply bitwise and with an immediate with a zero-extended immediate
    Value band_imm_u(Value value, std::uint64_t immediate) const noexcept {
        return Value{cl_ins_band_imm_u(builder_, value.raw(), immediate)};
    }

    // apply bitwise or with an immediate with a sign-extended immediate
    Value bor_imm_s(Value value, std::int64_t immediate) const noexcept {
        return Value{cl_ins_bor_imm_s(builder_, value.raw(), immediate)};
    }

    // apply bitwise or with an immediate with a zero-extended immediate
    Value bor_imm_u(Value value, std::uint64_t immediate) const noexcept {
        return Value{cl_ins_bor_imm_u(builder_, value.raw(), immediate)};
    }

    // apply bitwise xor with an immediate with a sign-extended immediate
    Value bxor_imm_s(Value value, std::int64_t immediate) const noexcept {
        return Value{cl_ins_bxor_imm_s(builder_, value.raw(), immediate)};
    }

    // apply bitwise xor with an immediate with a zero-extended immediate
    Value bxor_imm_u(Value value, std::uint64_t immediate) const noexcept {
        return Value{cl_ins_bxor_imm_u(builder_, value.raw(), immediate)};
    }

    // rotate an integer left by an immediate with a sign-extended immediate
    Value rotl_imm_s(Value value, std::int64_t immediate) const noexcept {
        return Value{cl_ins_rotl_imm_s(builder_, value.raw(), immediate)};
    }

    // rotate an integer left by an immediate with a zero-extended immediate
    Value rotl_imm_u(Value value, std::uint64_t immediate) const noexcept {
        return Value{cl_ins_rotl_imm_u(builder_, value.raw(), immediate)};
    }

    // rotate an integer right by an immediate with a sign-extended immediate
    Value rotr_imm_s(Value value, std::int64_t immediate) const noexcept {
        return Value{cl_ins_rotr_imm_s(builder_, value.raw(), immediate)};
    }

    // rotate an integer right by an immediate with a zero-extended immediate
    Value rotr_imm_u(Value value, std::uint64_t immediate) const noexcept {
        return Value{cl_ins_rotr_imm_u(builder_, value.raw(), immediate)};
    }

    // shift an integer left by an immediate with a sign-extended immediate
    Value ishl_imm_s(Value value, std::int64_t immediate) const noexcept {
        return Value{cl_ins_ishl_imm_s(builder_, value.raw(), immediate)};
    }

    // shift an integer left by an immediate with a zero-extended immediate
    Value ishl_imm_u(Value value, std::uint64_t immediate) const noexcept {
        return Value{cl_ins_ishl_imm_u(builder_, value.raw(), immediate)};
    }

    // shift an integer right without sign extension by an immediate with a sign-extended immediate
    Value ushr_imm_s(Value value, std::int64_t immediate) const noexcept {
        return Value{cl_ins_ushr_imm_s(builder_, value.raw(), immediate)};
    }

    // shift an integer right without sign extension by an immediate with a zero-extended immediate
    Value ushr_imm_u(Value value, std::uint64_t immediate) const noexcept {
        return Value{cl_ins_ushr_imm_u(builder_, value.raw(), immediate)};
    }

    // shift an integer right with sign extension by an immediate with a sign-extended immediate
    Value sshr_imm_s(Value value, std::int64_t immediate) const noexcept {
        return Value{cl_ins_sshr_imm_s(builder_, value.raw(), immediate)};
    }

    // shift an integer right with sign extension by an immediate with a zero-extended immediate
    Value sshr_imm_u(Value value, std::uint64_t immediate) const noexcept {
        return Value{cl_ins_sshr_imm_u(builder_, value.raw(), immediate)};
    }

    // the suffix selects immediate extension and the condition selects comparison signedness
    // scalar comparison results have type i8 and vector results use the corresponding lane mask type
    Value icmp_imm_s(IntCC condition, Value value, std::int64_t immediate) const noexcept {
        return Value{
            cl_ins_icmp_imm_s(builder_, static_cast<std::uint32_t>(condition), value.raw(), immediate)};
    }

    // compare against a zero-extended immediate with the supplied signed or unsigned condition
    Value icmp_imm_u(IntCC condition, Value value, std::uint64_t immediate) const noexcept {
        return Value{
            cl_ins_icmp_imm_u(builder_, static_cast<std::uint32_t>(condition), value.raw(), immediate)};
    }

    // compare with a signed immediate using the historical helper behavior
    Value icmp_imm(IntCC condition, Value value, std::int64_t immediate) const noexcept {
        return Value{
            cl_ins_icmp_imm(builder_, static_cast<std::uint32_t>(condition), value.raw(), immediate)};
    }

    // compare two integers using the given condition
    Value icmp(IntCC condition, Value left, Value right) const noexcept {
        return Value{cl_ins_icmp(builder_, static_cast<std::uint32_t>(condition), left.raw(), right.raw())};
    }

    // select one of two values according to a condition
    Value select(Value condition, Value if_true, Value if_false) const noexcept {
        return Value{cl_ins_select(builder_, condition.raw(), if_true.raw(), if_false.raw())};
    }

    // select each bit from if_true where the mask bit is set and from if_false otherwise
    // all operands must have the same type and the result is (mask & if_true) | (~mask & if_false)
    Value bitselect(Value mask, Value if_true, Value if_false) const noexcept {
        return Value{cl_ins_bitselect(builder_, mask.raw(), if_true.raw(), if_false.raw())};
    }

    // jump to a block and pass its block arguments
    Inst jump(Block destination, std::span<const Value> args = {}) const {
        const detail::RawValues raw_args(args);
        return Inst{cl_ins_jump(builder_, destination.raw(), raw_args.data(), args.size())};
    }

    // jump to a block with an inline list of block arguments
    Inst jump(Block destination, std::initializer_list<Value> args) const {
        return jump(destination, std::span<const Value>(args.begin(), args.size()));
    }

    // branch to one of two blocks and pass their block arguments
    Inst brif(Value condition, Block then_block, std::span<const Value> then_args, Block else_block,
              std::span<const Value> else_args) const {
        const detail::RawValues raw_then_args(then_args);
        const detail::RawValues raw_else_args(else_args);
        return Inst{cl_ins_brif(builder_, condition.raw(), then_block.raw(), raw_then_args.data(),
                                then_args.size(), else_block.raw(), raw_else_args.data(), else_args.size())};
    }

    // branch to one of two blocks with inline block argument lists
    Inst brif(Value condition, Block then_block, std::initializer_list<Value> then_args, Block else_block,
              std::initializer_list<Value> else_args) const {
        return brif(condition, then_block, std::span<const Value>(then_args.begin(), then_args.size()),
                    else_block, std::span<const Value>(else_args.begin(), else_args.size()));
    }

    // return values from the current function
    Inst return_(std::span<const Value> values = {}) const {
        const detail::RawValues raw_values(values);
        return Inst{cl_ins_return(builder_, raw_values.data(), values.size())};
    }

    // return one value without creating a temporary array
    Inst return_(Value value) const noexcept {
        const ClValue id = value.raw();
        return Inst{cl_ins_return(builder_, &id, 1)};
    }

    // return an inline list of values
    Inst return_(std::initializer_list<Value> values) const {
        return return_(std::span<const Value>(values.begin(), values.size()));
    }

    // call an imported function with the given arguments
    Inst call(FuncRef function, std::span<const Value> args = {}) const {
        const detail::RawValues raw_args(args);
        return Inst{cl_ins_call(builder_, function.raw(), raw_args.data(), args.size())};
    }

    // call an imported function with an inline argument list
    Inst call(FuncRef function, std::initializer_list<Value> args) const {
        return call(function, std::span<const Value>(args.begin(), args.size()));
    }

    // call a function pointer using an imported signature
    Inst call_indirect(SigRef signature, Value callee, std::span<const Value> args = {}) const {
        const detail::RawValues raw_args(args);
        return Inst{
            cl_ins_call_indirect(builder_, signature.raw(), callee.raw(), raw_args.data(), args.size())};
    }

    // call a function pointer with an inline argument list
    Inst call_indirect(SigRef signature, Value callee, std::initializer_list<Value> args) const {
        return call_indirect(signature, callee, std::span<const Value>(args.begin(), args.size()));
    }

    // load a typed value from an address and byte offset
    Value load(Type type, MemFlags flags, Value address, std::int32_t offset) const noexcept {
        return Value{cl_ins_load(builder_, type, flags, address.raw(), offset)};
    }

    // store a value at an address and byte offset
    Inst store(MemFlags flags, Value value, Value address, std::int32_t offset) const noexcept {
        return Inst{cl_ins_store(builder_, flags, value.raw(), address.raw(), offset)};
    }

    // emit a sequentially consistent fence that prevents loads and stores from crossing it
    Inst fence() const noexcept { return Inst{cl_ins_fence(builder_)}; }

    // atomic operations are sequentially consistent and accept scalar integer types
    // addresses must use the target pointer type and be naturally aligned for the accessed type
    // use flags from this function with native endianness and add offsets to the address explicitly
    // support for i128 atomics depends on the target and enabled cpu features

    // atomically load an integer of the supplied type
    Value atomic_load(Type type, MemFlags flags, Value address) const noexcept {
        return Value{cl_ins_atomic_load(builder_, type, flags, address.raw())};
    }

    // atomically store an integer using its value type as the memory access type
    Inst atomic_store(MemFlags flags, Value value, Value address) const noexcept {
        return Inst{cl_ins_atomic_store(builder_, flags, value.raw(), address.raw())};
    }

    // apply an operation to memory and return its old value with the supplied integer type
    // value must have the supplied type and xchg replaces memory with value
    Value atomic_rmw(Type type, MemFlags flags, AtomicRmwOp operation, Value address,
                     Value value) const noexcept {
        return Value{cl_ins_atomic_rmw(builder_, type, flags, static_cast<std::uint32_t>(operation),
                                      address.raw(), value.raw())};
    }

    // store replacement only if memory equals expected and always return the old memory value
    // expected and replacement must have the same integer type and old == expected means success
    Value atomic_cas(MemFlags flags, Value address, Value expected, Value replacement) const noexcept {
        return Value{cl_ins_atomic_cas(builder_, flags, address.raw(), expected.raw(), replacement.raw())};
    }

private:
    ClFunctionBuilder* builder_;
};

class FunctionBuilder {
public:
    // keep both contexts alive until finish consumes this builder
    // borrow a codegen context and reusable builder context to build one function
    FunctionBuilder(Context& context, FunctionBuilderContext& builder_context) noexcept
        : handle_(cl_function_builder_new(context.raw(), builder_context.raw())), context_(&context) {}

    void comment(Inst instruction, std::string text) { context_->comment(instruction, std::move(text)); }
    bool comment(std::string text) {
        ClBlock block;
        ClInst previous;
        if (!cl_builder_comment_position(raw(), &block, &previous))
            return false;
        context_->comment_at(Block{block}, Inst{previous}, std::move(text));
        return true;
    }

    // return the underlying function builder without transferring ownership
    ClFunctionBuilder* raw() const noexcept { return handle_.get(); }

    // report whether the function builder was created successfully
    explicit operator bool() const noexcept { return static_cast<bool>(handle_); }

    // complete a sealed function and release its borrow of both contexts
    // do not use this builder after finish
    void finish(const JITModule& module) noexcept {
        cl_function_builder_finish(handle_.release(), module.raw());
    }

    // create a new basic block
    Block create_block() noexcept { return Block{cl_builder_create_block(raw())}; }

    // select the block that receives subsequent instructions
    void switch_to_block(Block block) noexcept { cl_builder_switch_to_block(raw(), block.raw()); }

    // seal a block after all predecessors are known
    void seal_block(Block block) noexcept { cl_builder_seal_block(raw(), block.raw()); }

    // seal every block after all predecessor edges have been emitted
    void seal_all_blocks() noexcept { cl_builder_seal_all_blocks(raw()); }

    // append the function parameters to a block
    void append_block_params_for_function_params(Block block) noexcept {
        cl_builder_append_block_params_for_function_params(raw(), block.raw());
    }

    // append a typed parameter to a block and return its value id
    Value append_block_param(Block block, Type type) noexcept {
        return Value{cl_builder_append_block_param(raw(), block.raw(), type)};
    }

    // return a block parameter by index
    Value block_param(Block block, std::size_t index) const noexcept {
        return Value{cl_builder_block_param(raw(), block.raw(), index)};
    }

    // declare a mutable variable of the given type
    Variable declare_var(Type type) noexcept { return Variable{cl_builder_declare_var(raw(), type)}; }

    // assign a value to a declared variable
    void def_var(Variable variable, Value value) noexcept {
        cl_builder_def_var(raw(), variable.raw(), value.raw());
    }

    // read the current value of a declared variable
    Value use_var(Variable variable) noexcept { return Value{cl_builder_use_var(raw(), variable.raw())}; }

    // return one result of an inserted instruction or INVALID_ID for a missing index
    Value inst_result(Inst instruction, std::size_t index) const noexcept {
        return Value{cl_builder_inst_result(raw(), instruction.raw(), index)};
    }

    // import a signature for indirect calls
    SigRef import_signature(const Signature& signature) noexcept {
        return SigRef{cl_builder_import_signature(raw(), signature.raw())};
    }

    // import a module function into the current function
    FuncRef declare_func_in_func(JITModule& module, FuncId id) noexcept {
        return FuncRef{cl_builder_declare_func_in_func(module.raw(), id.raw(), raw())};
    }

    // import a module data object into the current function
    GlobalValue declare_data_in_func(const JITModule& module, DataId id) noexcept {
        return GlobalValue{cl_builder_declare_data_in_func(module.raw(), id.raw(), raw())};
    }

    // create memory access flags for this function
    // trusted means the address is proven aligned and unable to trap
    MemFlags memflags_new(bool trusted = false) noexcept { return cl_memflags_new(raw(), trusted); }

    // create flags with the requested byte order for guest memory accesses
    MemFlags memflags_with_endianness(MemFlags flags, Endianness endianness) noexcept {
        return cl_memflags_with_endianness(raw(), flags, static_cast<std::uint32_t>(endianness));
    }

    // return an instruction builder for the current block
    InstBuilder ins() noexcept { return InstBuilder(raw()); }

private:
    detail::Owned<ClFunctionBuilder, cl_function_builder_drop> handle_;
    Context* context_;
};

}  // namespace cranelift
