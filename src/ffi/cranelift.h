#pragma once

#include "cranelift_ffi.h"

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
using Value = ClValue;
using Block = ClBlock;
using Inst = ClInst;
using Variable = ClVariable;
using FuncRef = ClFuncRef;
using FuncId = ClFuncId;
using DataId = ClDataId;
using GlobalValue = ClGlobalValue;
using SigRef = ClSigRef;
using MemFlags = ClMemFlags;
using Linkage = ClLinkage;
using IntCC = ClIntCC;
using Endianness = ClEndianness;
using CallConv = ClCallConv;

inline constexpr std::uint32_t INVALID_ID = CL_INVALID_ID;

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
        const std::size_t required = cl_context_display(raw(), nullptr, 0);
        if (required == 0)
            return {};
        std::string result(required, '\0');
        if (cl_context_display(raw(), result.data(), result.size()) == 0)
            return {};
        result.pop_back();
        return result;
    }

private:
    detail::Owned<ClContext, cl_context_drop> handle_;
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
    void clear_context(Context& context) const noexcept { cl_module_clear_context(raw(), context.raw()); }

    // create a signature with the module calling convention
    Signature make_signature() const noexcept { return Signature(cl_module_make_signature(raw()), true); }

    // clear a signature for reuse with this module
    void clear_signature(Signature& signature) const noexcept {
        cl_module_clear_signature(raw(), signature.raw());
    }

    // declare a named function and return its module identifier
    FuncId declare_function(const char* name, Linkage linkage, const Signature& signature) noexcept {
        return cl_module_declare_function(raw(), name, static_cast<std::uint32_t>(linkage), signature.raw());
    }

    // import a declared function into the given codegen context
    FuncRef declare_func_in_func(FuncId id, Context& context) noexcept {
        return cl_module_declare_func_in_func(raw(), id, context.raw());
    }

    // compile and define a declared function from the codegen context once per func id
    bool define_function(FuncId id, Context& context) noexcept {
        return cl_module_define_function(raw(), id, context.raw());
    }

    // declare a named data object and return its module identifier
    DataId declare_data(const char* name, Linkage linkage, bool writable, bool tls) noexcept {
        return cl_module_declare_data(raw(), name, static_cast<std::uint32_t>(linkage), writable, tls);
    }

    // import a declared data object into the given codegen context
    GlobalValue declare_data_in_func(DataId id, Context& context) const noexcept {
        return cl_module_declare_data_in_func(raw(), id, context.raw());
    }

    // define a declared data object from its description
    bool define_data(DataId id, const DataDescription& description) noexcept {
        return cl_module_define_data(raw(), id, description.raw());
    }

    // apply relocations and make all pending definitions callable or readable
    bool finalize_definitions() noexcept { return cl_jit_module_finalize_definitions(raw()); }

    // return the address of a compiled and finalized function
    // calling before finalization or with an invalid id can abort in the rust layer
    const void* get_finalized_function(FuncId id) const noexcept {
        return cl_jit_module_get_finalized_function(raw(), id);
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
        const void* address = cl_jit_module_get_finalized_data(raw(), id, &size);
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
    // borrow the active function builder for instruction insertion
    explicit InstBuilder(ClFunctionBuilder* builder) noexcept : builder_(builder) {}

    // insert an integer constant with the given type
    Value iconst(Type type, std::int64_t immediate) const noexcept {
        return cl_ins_iconst(builder_, type, immediate);
    }

    // insert an f32 constant from its bit pattern
    Value f32const(std::uint32_t bits) const noexcept { return cl_ins_f32const(builder_, bits); }

    // insert an f64 constant from its bit pattern
    Value f64const(std::uint64_t bits) const noexcept { return cl_ins_f64const(builder_, bits); }

    // insert the address of an imported global value
    Value symbol_value(Type type, GlobalValue global) const noexcept {
        return cl_ins_symbol_value(builder_, type, global);
    }

    // define direct wrappers for binary cranelift instructions
#define CL_BINARY_METHOD(name)                                                                               \
    Value name(Value left, Value right) const noexcept { return cl_ins_##name(builder_, left, right); }

    // add two integer values
    CL_BINARY_METHOD(iadd)

    // subtract the right integer from the left integer
    CL_BINARY_METHOD(isub)

    // multiply two integer values
    CL_BINARY_METHOD(imul)

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

    // negate an integer value
    Value ineg(Value value) const noexcept { return cl_ins_ineg(builder_, value); }

    // invert every bit of an integer value
    Value bnot(Value value) const noexcept { return cl_ins_bnot(builder_, value); }

    // reverse the byte order of an integer value
    Value bswap(Value value) const noexcept { return cl_ins_bswap(builder_, value); }

    // narrow an integer value to the given type
    Value ireduce(Type type, Value value) const noexcept { return cl_ins_ireduce(builder_, type, value); }

    // widen an integer value with zero extension
    Value uextend(Type type, Value value) const noexcept { return cl_ins_uextend(builder_, type, value); }

    // widen an integer value with sign extension
    Value sextend(Type type, Value value) const noexcept { return cl_ins_sextend(builder_, type, value); }

    // add a signed immediate to an integer value
    Value iadd_imm(Value value, std::int64_t immediate) const noexcept {
        return cl_ins_iadd_imm(builder_, value, immediate);
    }

    // compare two integers using the given condition
    Value icmp(IntCC condition, Value left, Value right) const noexcept {
        return cl_ins_icmp(builder_, static_cast<std::uint32_t>(condition), left, right);
    }

    // select one of two values according to a condition
    Value select(Value condition, Value if_true, Value if_false) const noexcept {
        return cl_ins_select(builder_, condition, if_true, if_false);
    }

    // jump to a block and pass its block arguments
    Inst jump(Block destination, std::span<const Value> args = {}) const noexcept {
        return cl_ins_jump(builder_, destination, args.data(), args.size());
    }

    // jump to a block with an inline list of block arguments
    Inst jump(Block destination, std::initializer_list<Value> args) const noexcept {
        return jump(destination, std::span<const Value>(args.begin(), args.size()));
    }

    // branch to one of two blocks and pass their block arguments
    Inst brif(Value condition, Block then_block, std::span<const Value> then_args, Block else_block,
              std::span<const Value> else_args) const noexcept {
        return cl_ins_brif(builder_, condition, then_block, then_args.data(), then_args.size(), else_block,
                           else_args.data(), else_args.size());
    }

    // branch to one of two blocks with inline block argument lists
    Inst brif(Value condition, Block then_block, std::initializer_list<Value> then_args, Block else_block,
              std::initializer_list<Value> else_args) const noexcept {
        return brif(condition, then_block, std::span<const Value>(then_args.begin(), then_args.size()),
                    else_block, std::span<const Value>(else_args.begin(), else_args.size()));
    }

    // return values from the current function
    Inst return_(std::span<const Value> values = {}) const noexcept {
        return cl_ins_return(builder_, values.data(), values.size());
    }

    // return one value without creating a temporary array
    Inst return_(Value value) const noexcept { return cl_ins_return(builder_, &value, 1); }

    // return an inline list of values
    Inst return_(std::initializer_list<Value> values) const noexcept {
        return return_(std::span<const Value>(values.begin(), values.size()));
    }

    // call an imported function with the given arguments
    Inst call(FuncRef function, std::span<const Value> args = {}) const noexcept {
        return cl_ins_call(builder_, function, args.data(), args.size());
    }

    // call an imported function with an inline argument list
    Inst call(FuncRef function, std::initializer_list<Value> args) const noexcept {
        return call(function, std::span<const Value>(args.begin(), args.size()));
    }

    // call a function pointer using an imported signature
    Inst call_indirect(SigRef signature, Value callee, std::span<const Value> args = {}) const noexcept {
        return cl_ins_call_indirect(builder_, signature, callee, args.data(), args.size());
    }

    // call a function pointer with an inline argument list
    Inst call_indirect(SigRef signature, Value callee, std::initializer_list<Value> args) const noexcept {
        return call_indirect(signature, callee, std::span<const Value>(args.begin(), args.size()));
    }

    // load a typed value from an address and byte offset
    Value load(Type type, MemFlags flags, Value address, std::int32_t offset) const noexcept {
        return cl_ins_load(builder_, type, flags, address, offset);
    }

    // store a value at an address and byte offset
    Inst store(MemFlags flags, Value value, Value address, std::int32_t offset) const noexcept {
        return cl_ins_store(builder_, flags, value, address, offset);
    }

private:
    ClFunctionBuilder* builder_;
};

class FunctionBuilder {
public:
    // keep both contexts alive until finish consumes this builder
    // borrow a codegen context and reusable builder context to build one function
    FunctionBuilder(Context& context, FunctionBuilderContext& builder_context) noexcept
        : handle_(cl_function_builder_new(context.raw(), builder_context.raw())) {}

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
    Block create_block() noexcept { return cl_builder_create_block(raw()); }

    // select the block that receives subsequent instructions
    void switch_to_block(Block block) noexcept { cl_builder_switch_to_block(raw(), block); }

    // seal a block after all predecessors are known
    void seal_block(Block block) noexcept { cl_builder_seal_block(raw(), block); }

    // seal every block after all predecessor edges have been emitted
    void seal_all_blocks() noexcept { cl_builder_seal_all_blocks(raw()); }

    // append the function parameters to a block
    void append_block_params_for_function_params(Block block) noexcept {
        cl_builder_append_block_params_for_function_params(raw(), block);
    }

    // append a typed parameter to a block and return its value id
    Value append_block_param(Block block, Type type) noexcept {
        return cl_builder_append_block_param(raw(), block, type);
    }

    // return a block parameter by index
    Value block_param(Block block, std::size_t index) const noexcept {
        return cl_builder_block_param(raw(), block, index);
    }

    // declare a mutable variable of the given type
    Variable declare_var(Type type) noexcept { return cl_builder_declare_var(raw(), type); }

    // assign a value to a declared variable
    void def_var(Variable variable, Value value) noexcept { cl_builder_def_var(raw(), variable, value); }

    // read the current value of a declared variable
    Value use_var(Variable variable) noexcept { return cl_builder_use_var(raw(), variable); }

    // return one result of an inserted instruction or INVALID_ID for a missing index
    Value inst_result(Inst instruction, std::size_t index) const noexcept {
        return cl_builder_inst_result(raw(), instruction, index);
    }

    // import a signature for indirect calls
    SigRef import_signature(const Signature& signature) noexcept {
        return cl_builder_import_signature(raw(), signature.raw());
    }

    // import a module function into the current function
    FuncRef declare_func_in_func(JITModule& module, FuncId id) noexcept {
        return cl_builder_declare_func_in_func(module.raw(), id, raw());
    }

    // import a module data object into the current function
    GlobalValue declare_data_in_func(const JITModule& module, DataId id) noexcept {
        return cl_builder_declare_data_in_func(module.raw(), id, raw());
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
};

}  // namespace cranelift
