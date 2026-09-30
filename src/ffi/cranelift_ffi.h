#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
namespace cranelift {
extern "C" {
#endif

// Pinned to the Cranelift 0.135.2 entity and Type representations.
// Rust-owned values are opaque. Entity IDs belong to the function/module that created them.
typedef struct ClSettingsBuilder ClSettingsBuilder;
typedef struct ClNativeBuilder ClNativeBuilder;
typedef struct ClIsa ClIsa;
typedef struct ClJitBuilder ClJitBuilder;
typedef struct ClJitModule ClJitModule;
typedef struct ClContext ClContext;
typedef struct ClSignature ClSignature;
typedef struct ClFunctionBuilderContext ClFunctionBuilderContext;
typedef struct ClFunctionBuilder ClFunctionBuilder;
typedef struct ClDataDescription ClDataDescription;

typedef uint16_t ClType;
typedef uint32_t ClValue;
typedef uint32_t ClBlock;
typedef uint32_t ClInst;
typedef uint32_t ClVariable;
typedef uint32_t ClFuncRef;
typedef uint32_t ClFuncId;
typedef uint32_t ClDataId;
typedef uint32_t ClGlobalValue;
typedef uint32_t ClSigRef;
typedef uint32_t ClMemFlags;

#define CL_INVALID_ID UINT32_MAX

typedef enum ClLinkage {
    CL_LINKAGE_IMPORT = 0,
    CL_LINKAGE_LOCAL = 1,
    CL_LINKAGE_PREEMPTIBLE = 2,
    CL_LINKAGE_HIDDEN = 3,
    CL_LINKAGE_EXPORT = 4
} ClLinkage;

typedef enum ClIntCC {
    CL_INTCC_EQUAL = 0,
    CL_INTCC_NOT_EQUAL = 1,
    CL_INTCC_SIGNED_LESS_THAN = 2,
    CL_INTCC_SIGNED_GREATER_THAN_OR_EQUAL = 3,
    CL_INTCC_SIGNED_GREATER_THAN = 4,
    CL_INTCC_SIGNED_LESS_THAN_OR_EQUAL = 5,
    CL_INTCC_UNSIGNED_LESS_THAN = 6,
    CL_INTCC_UNSIGNED_GREATER_THAN_OR_EQUAL = 7,
    CL_INTCC_UNSIGNED_GREATER_THAN = 8,
    CL_INTCC_UNSIGNED_LESS_THAN_OR_EQUAL = 9
} ClIntCC;

typedef enum ClEndianness { CL_ENDIANNESS_LITTLE = 0, CL_ENDIANNESS_BIG = 1 } ClEndianness;

typedef enum ClCallConv {
    CL_CALL_CONV_FAST = 0,
    CL_CALL_CONV_TAIL = 1,
    CL_CALL_CONV_SYSTEM_V = 2,
    CL_CALL_CONV_WINDOWS_FASTCALL = 3,
    CL_CALL_CONV_APPLE_AARCH64 = 4,
    CL_CALL_CONV_PROBESTACK = 5,
    CL_CALL_CONV_WINCH = 6,
    CL_CALL_CONV_PRESERVE_ALL = 7
} ClCallConv;

// Error text is thread-local and remains valid until the next failing call on that thread.
const char* cl_last_error(void);

ClType cl_type_i8(void);
ClType cl_type_i16(void);
ClType cl_type_i32(void);
ClType cl_type_i64(void);
ClType cl_type_i128(void);
ClType cl_type_f16(void);
ClType cl_type_f32(void);
ClType cl_type_f64(void);
ClType cl_type_f128(void);
// return a scalar integer type or zero when the width is unsupported
ClType cl_type_int(uint16_t bits);
// return a fixed simd type from a scalar lane type and a power of two lane count
ClType cl_type_vector(ClType lane_type, uint32_t lanes);
// convert a fixed vector of at most 256 bits to its dynamic vector type
ClType cl_type_vector_to_dynamic(ClType fixed_vector_type);

ClSettingsBuilder* cl_settings_builder_new(void);
void cl_settings_builder_drop(ClSettingsBuilder* builder);
bool cl_settings_set(ClSettingsBuilder* builder, const char* name, const char* value);
bool cl_settings_enable(ClSettingsBuilder* builder, const char* name);
ClNativeBuilder* cl_native_builder(void);
ClNativeBuilder* cl_native_builder_with_options(bool infer_native_flags);
void cl_native_builder_drop(ClNativeBuilder* builder);
bool cl_native_set(ClNativeBuilder* builder, const char* name, const char* value);
bool cl_native_enable(ClNativeBuilder* builder, const char* name);
// Consumes builder and flags, including on failure.
ClIsa* cl_isa_finish(ClNativeBuilder* builder, ClSettingsBuilder* flags);
void cl_isa_drop(ClIsa* isa);

ClJitBuilder* cl_jit_builder_new(void);
ClJitBuilder* cl_jit_builder_with_flags(const char* const* names, const char* const* values, size_t len);
// Consumes isa.
ClJitBuilder* cl_jit_builder_with_isa(ClIsa* isa);
bool cl_jit_builder_symbol(ClJitBuilder* builder, const char* name, const void* address);
void cl_jit_builder_drop(ClJitBuilder* builder);
// Consumes builder.
ClJitModule* cl_jit_module_new(ClJitBuilder* builder);
void cl_jit_module_drop(ClJitModule* module);
// Consumes module and invalidates every function pointer obtained from it.
// Call only when no compiled function is executing.
void cl_jit_module_free_memory(ClJitModule* module);

ClContext* cl_module_make_context(const ClJitModule* module);
ClType cl_module_pointer_type(const ClJitModule* module);
void cl_module_clear_context(const ClJitModule* module, ClContext* context);
void cl_context_drop(ClContext* context);
bool cl_context_verify(const ClContext* context, const ClJitModule* module);
// return the required buffer size including the null terminator and optionally copy the function ir
// pass a null buffer and zero capacity to query the size
size_t cl_context_display(const ClContext* context, char* buffer, size_t capacity);
// Borrowed from context; do not drop or retain after context is cleared/dropped.
ClSignature* cl_context_signature(ClContext* context);
ClSignature* cl_module_make_signature(const ClJitModule* module);
void cl_module_clear_signature(const ClJitModule* module, ClSignature* signature);
void cl_signature_drop(ClSignature* signature);
void cl_signature_push_param(ClSignature* signature, ClType type);
void cl_signature_push_return(ClSignature* signature, ClType type);
bool cl_signature_set_call_conv(ClSignature* signature, uint32_t convention);

ClFuncId cl_module_declare_function(ClJitModule* module, const char* name, uint32_t linkage,
                                    const ClSignature* signature);
ClFuncRef cl_module_declare_func_in_func(ClJitModule* module, ClFuncId id, ClContext* context);
bool cl_module_define_function(ClJitModule* module, ClFuncId id, ClContext* context);
bool cl_jit_module_finalize_definitions(ClJitModule* module);
const void* cl_jit_module_get_finalized_function(const ClJitModule* module, ClFuncId id);
const void* cl_jit_module_get_finalized_data(const ClJitModule* module, ClDataId id, size_t* size);

ClDataDescription* cl_data_description_new(void);
void cl_data_description_drop(ClDataDescription* description);
void cl_data_description_clear(ClDataDescription* description);
void cl_data_description_define_zeroinit(ClDataDescription* description, size_t size);
bool cl_data_description_define(ClDataDescription* description, const uint8_t* bytes, size_t size);
bool cl_data_description_set_align(ClDataDescription* description, uint64_t align);
ClDataId cl_module_declare_data(ClJitModule* module, const char* name, uint32_t linkage, bool writable,
                                bool tls);
bool cl_module_define_data(ClJitModule* module, ClDataId id, const ClDataDescription* description);
ClGlobalValue cl_module_declare_data_in_func(const ClJitModule* module, ClDataId id, ClContext* context);

ClFunctionBuilderContext* cl_function_builder_context_new(void);
void cl_function_builder_context_drop(ClFunctionBuilderContext* context);
// The context and builder context must remain alive and unused elsewhere until finish.
ClFunctionBuilder* cl_function_builder_new(ClContext* context, ClFunctionBuilderContext* builder_context);
// Discards an unfinished builder. Its builder context must be discarded too.
void cl_function_builder_drop(ClFunctionBuilder* builder);
// Consumes builder. The module provides Cranelift's TargetFrontendConfig.
void cl_function_builder_finish(ClFunctionBuilder* builder, const ClJitModule* module);
ClBlock cl_builder_create_block(ClFunctionBuilder* builder);
void cl_builder_switch_to_block(ClFunctionBuilder* builder, ClBlock block);
void cl_builder_seal_block(ClFunctionBuilder* builder, ClBlock block);
void cl_builder_seal_all_blocks(ClFunctionBuilder* builder);
void cl_builder_append_block_params_for_function_params(ClFunctionBuilder* builder, ClBlock block);
ClValue cl_builder_append_block_param(ClFunctionBuilder* builder, ClBlock block, ClType type);
ClValue cl_builder_block_param(const ClFunctionBuilder* builder, ClBlock block, size_t index);
ClVariable cl_builder_declare_var(ClFunctionBuilder* builder, ClType type);
void cl_builder_def_var(ClFunctionBuilder* builder, ClVariable variable, ClValue value);
ClValue cl_builder_use_var(ClFunctionBuilder* builder, ClVariable variable);
ClValue cl_builder_inst_result(const ClFunctionBuilder* builder, ClInst instruction, size_t index);
ClSigRef cl_builder_import_signature(ClFunctionBuilder* builder, const ClSignature* signature);
ClFuncRef cl_builder_declare_func_in_func(ClJitModule* module, ClFuncId id, ClFunctionBuilder* builder);
ClGlobalValue cl_builder_declare_data_in_func(const ClJitModule* module, ClDataId id,
                                              ClFunctionBuilder* builder);

ClValue cl_ins_iconst(ClFunctionBuilder* builder, ClType type, int64_t immediate);
ClValue cl_ins_f32const(ClFunctionBuilder* builder, uint32_t bits);
ClValue cl_ins_f64const(ClFunctionBuilder* builder, uint64_t bits);
ClValue cl_ins_symbol_value(ClFunctionBuilder* builder, ClType type, ClGlobalValue global);
ClValue cl_ins_iadd(ClFunctionBuilder* builder, ClValue left, ClValue right);
ClValue cl_ins_isub(ClFunctionBuilder* builder, ClValue left, ClValue right);
ClValue cl_ins_imul(ClFunctionBuilder* builder, ClValue left, ClValue right);
ClValue cl_ins_band(ClFunctionBuilder* builder, ClValue left, ClValue right);
ClValue cl_ins_bor(ClFunctionBuilder* builder, ClValue left, ClValue right);
ClValue cl_ins_bxor(ClFunctionBuilder* builder, ClValue left, ClValue right);
ClValue cl_ins_ishl(ClFunctionBuilder* builder, ClValue left, ClValue right);
ClValue cl_ins_ushr(ClFunctionBuilder* builder, ClValue left, ClValue right);
ClValue cl_ins_sshr(ClFunctionBuilder* builder, ClValue left, ClValue right);
ClValue cl_ins_udiv(ClFunctionBuilder* builder, ClValue left, ClValue right);
ClValue cl_ins_sdiv(ClFunctionBuilder* builder, ClValue left, ClValue right);
ClValue cl_ins_urem(ClFunctionBuilder* builder, ClValue left, ClValue right);
ClValue cl_ins_srem(ClFunctionBuilder* builder, ClValue left, ClValue right);
ClValue cl_ins_rotl(ClFunctionBuilder* builder, ClValue left, ClValue right);
ClValue cl_ins_rotr(ClFunctionBuilder* builder, ClValue left, ClValue right);
ClValue cl_ins_fadd(ClFunctionBuilder* builder, ClValue left, ClValue right);
ClValue cl_ins_fsub(ClFunctionBuilder* builder, ClValue left, ClValue right);
ClValue cl_ins_fmul(ClFunctionBuilder* builder, ClValue left, ClValue right);
ClValue cl_ins_fdiv(ClFunctionBuilder* builder, ClValue left, ClValue right);
ClValue cl_ins_ineg(ClFunctionBuilder* builder, ClValue value);
ClValue cl_ins_bnot(ClFunctionBuilder* builder, ClValue value);
ClValue cl_ins_bswap(ClFunctionBuilder* builder, ClValue value);
ClValue cl_ins_ireduce(ClFunctionBuilder* builder, ClType type, ClValue value);
ClValue cl_ins_uextend(ClFunctionBuilder* builder, ClType type, ClValue value);
ClValue cl_ins_sextend(ClFunctionBuilder* builder, ClType type, ClValue value);
ClValue cl_ins_iadd_imm(ClFunctionBuilder* builder, ClValue value, int64_t immediate);
ClValue cl_ins_icmp(ClFunctionBuilder* builder, uint32_t condition, ClValue left, ClValue right);
ClValue cl_ins_select(ClFunctionBuilder* builder, ClValue condition, ClValue if_true, ClValue if_false);
ClInst cl_ins_jump(ClFunctionBuilder* builder, ClBlock destination, const ClValue* args, size_t len);
ClInst cl_ins_brif(ClFunctionBuilder* builder, ClValue condition, ClBlock then_block,
                   const ClValue* then_args, size_t then_len, ClBlock else_block, const ClValue* else_args,
                   size_t else_len);
ClInst cl_ins_return(ClFunctionBuilder* builder, const ClValue* args, size_t len);
ClInst cl_ins_call(ClFunctionBuilder* builder, ClFuncRef function, const ClValue* args, size_t len);
ClInst cl_ins_call_indirect(ClFunctionBuilder* builder, ClSigRef signature, ClValue callee,
                            const ClValue* args, size_t len);
ClMemFlags cl_memflags_new(ClFunctionBuilder* builder, bool trusted);
ClMemFlags cl_memflags_with_endianness(ClFunctionBuilder* builder, ClMemFlags flags, uint32_t endianness);
ClValue cl_ins_load(ClFunctionBuilder* builder, ClType type, ClMemFlags flags, ClValue address,
                    int32_t offset);
ClInst cl_ins_store(ClFunctionBuilder* builder, ClMemFlags flags, ClValue value, ClValue address,
                    int32_t offset);

#ifdef __cplusplus
}
}  // namespace cranelift
#endif
