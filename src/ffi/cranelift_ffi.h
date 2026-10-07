#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
namespace cranelift {
extern "C" {
#endif

// pinned to the cranelift 0.136.2 entity and type representations
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

// two instruction result ids in the same order as the rust tuple
typedef struct ClValuePair {
    ClValue first;
    ClValue second;
} ClValuePair;
typedef uint32_t ClBlock;
typedef uint32_t ClInst;
typedef uint32_t ClVariable;
typedef uint32_t ClFuncRef;
typedef uint32_t ClFuncId;
typedef uint32_t ClDataId;
typedef uint32_t ClGlobalValue;
typedef uint32_t ClSigRef;
typedef uint32_t ClMemFlags;

// nonzero raw cranelift trap code with user codes from 1 through 250
typedef uint8_t ClTrapCode;
typedef enum ClBuiltinTrapCode {
    CL_TRAP_STACK_OVERFLOW = 251,
    CL_TRAP_INTEGER_OVERFLOW = 252,
    CL_TRAP_HEAP_OUT_OF_BOUNDS = 253,
    CL_TRAP_INTEGER_DIVISION_BY_ZERO = 254,
    CL_TRAP_BAD_CONVERSION_TO_INTEGER = 255
} ClBuiltinTrapCode;

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

// ordered comparisons exclude nan and unordered comparisons include nan
typedef enum ClFloatCC {
    CL_FLOATCC_ORDERED = 0,
    CL_FLOATCC_UNORDERED = 1,
    CL_FLOATCC_EQUAL = 2,
    CL_FLOATCC_NOT_EQUAL = 3,
    CL_FLOATCC_ORDERED_NOT_EQUAL = 4,
    CL_FLOATCC_UNORDERED_OR_EQUAL = 5,
    CL_FLOATCC_LESS_THAN = 6,
    CL_FLOATCC_LESS_THAN_OR_EQUAL = 7,
    CL_FLOATCC_GREATER_THAN = 8,
    CL_FLOATCC_GREATER_THAN_OR_EQUAL = 9,
    CL_FLOATCC_UNORDERED_OR_LESS_THAN = 10,
    CL_FLOATCC_UNORDERED_OR_LESS_THAN_OR_EQUAL = 11,
    CL_FLOATCC_UNORDERED_OR_GREATER_THAN = 12,
    CL_FLOATCC_UNORDERED_OR_GREATER_THAN_OR_EQUAL = 13
} ClFloatCC;

typedef enum ClEndianness { CL_ENDIANNESS_LITTLE = 0, CL_ENDIANNESS_BIG = 1 } ClEndianness;

// operation applied to memory by atomic_rmw which always returns the old value
typedef enum ClAtomicRmwOp {
    CL_ATOMIC_RMW_ADD = 0,
    CL_ATOMIC_RMW_SUB = 1,
    CL_ATOMIC_RMW_AND = 2,
    CL_ATOMIC_RMW_NAND = 3,
    CL_ATOMIC_RMW_OR = 4,
    CL_ATOMIC_RMW_XOR = 5,
    CL_ATOMIC_RMW_XCHG = 6,
    CL_ATOMIC_RMW_UMIN = 7,
    CL_ATOMIC_RMW_UMAX = 8,
    CL_ATOMIC_RMW_SMIN = 9,
    CL_ATOMIC_RMW_SMAX = 10
} ClAtomicRmwOp;

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
// construct a user trap code or return zero and set the thread error for an invalid or reserved code
ClTrapCode cl_trapcode_user(uint8_t code);

ClType cl_type_i8(void);
ClType cl_type_i16(void);
ClType cl_type_i32(void);
ClType cl_type_i64(void);
ClType cl_type_i128(void);
ClType cl_type_f16(void);
ClType cl_type_f32(void);
ClType cl_type_f64(void);
ClType cl_type_f128(void);
// fixed vector types with instruction support depending on the target backend and cpu features
// return the cranelift i8x2 type with 2 i8 lanes
ClType cl_type_i8x2(void);
// return the cranelift i8x4 type with 4 i8 lanes
ClType cl_type_i8x4(void);
// return the cranelift i16x2 type with 2 i16 lanes
ClType cl_type_i16x2(void);
// return the cranelift f16x2 type with 2 f16 lanes
ClType cl_type_f16x2(void);
// return the cranelift i8x8 type with 8 i8 lanes
ClType cl_type_i8x8(void);
// return the cranelift i16x4 type with 4 i16 lanes
ClType cl_type_i16x4(void);
// return the cranelift i32x2 type with 2 i32 lanes
ClType cl_type_i32x2(void);
// return the cranelift f16x4 type with 4 f16 lanes
ClType cl_type_f16x4(void);
// return the cranelift f32x2 type with 2 f32 lanes
ClType cl_type_f32x2(void);
// return the cranelift i8x16 type with 16 i8 lanes
ClType cl_type_i8x16(void);
// return the cranelift i16x8 type with 8 i16 lanes
ClType cl_type_i16x8(void);
// return the cranelift i32x4 type with 4 i32 lanes
ClType cl_type_i32x4(void);
// return the cranelift i64x2 type with 2 i64 lanes
ClType cl_type_i64x2(void);
// return the cranelift f16x8 type with 8 f16 lanes
ClType cl_type_f16x8(void);
// return the cranelift f32x4 type with 4 f32 lanes
ClType cl_type_f32x4(void);
// return the cranelift f64x2 type with 2 f64 lanes
ClType cl_type_f64x2(void);
// return the cranelift i8x32 type with 32 i8 lanes
ClType cl_type_i8x32(void);
// return the cranelift i16x16 type with 16 i16 lanes
ClType cl_type_i16x16(void);
// return the cranelift i32x8 type with 8 i32 lanes
ClType cl_type_i32x8(void);
// return the cranelift i64x4 type with 4 i64 lanes
ClType cl_type_i64x4(void);
// return the cranelift i128x2 type with 2 i128 lanes
ClType cl_type_i128x2(void);
// return the cranelift f16x16 type with 16 f16 lanes
ClType cl_type_f16x16(void);
// return the cranelift f32x8 type with 8 f32 lanes
ClType cl_type_f32x8(void);
// return the cranelift f64x4 type with 4 f64 lanes
ClType cl_type_f64x4(void);
// return the cranelift f128x2 type with 2 f128 lanes
ClType cl_type_f128x2(void);
// return the cranelift i8x64 type with 64 i8 lanes
ClType cl_type_i8x64(void);
// return the cranelift i16x32 type with 32 i16 lanes
ClType cl_type_i16x32(void);
// return the cranelift i32x16 type with 16 i32 lanes
ClType cl_type_i32x16(void);
// return the cranelift i64x8 type with 8 i64 lanes
ClType cl_type_i64x8(void);
// return the cranelift i128x4 type with 4 i128 lanes
ClType cl_type_i128x4(void);
// return the cranelift f16x32 type with 32 f16 lanes
ClType cl_type_f16x32(void);
// return the cranelift f32x16 type with 16 f32 lanes
ClType cl_type_f32x16(void);
// return the cranelift f64x8 type with 8 f64 lanes
ClType cl_type_f64x8(void);
// return the cranelift f128x4 type with 4 f128 lanes
ClType cl_type_f128x4(void);
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

typedef struct ClIRComment {
    uint32_t instruction;
    uint32_t block;  // INVALID means before instruction; otherwise after instruction in block.
    const char* text;
} ClIRComment;

bool cl_builder_comment_position(const ClFunctionBuilder* builder, uint32_t* block, uint32_t* instruction);
size_t cl_context_display_with_comments(const ClContext* context, const ClIRComment* comments, size_t count,
                                        char* buffer, size_t capacity);

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

// insert a scalar i8 i16 i32 or i64 constant and use splat for vector constants
// return the invalid value and set last_error for an unsupported type
ClValue cl_ins_iconst(ClFunctionBuilder* builder, ClType type, int64_t immediate);
// create a fixed vector constant from readable bytes copied into the function constant pool
// len must match the vector size in bytes and instruction support depends on the target backend
// return the invalid value and set last_error for a scalar type wrong size or null bytes
ClValue cl_ins_vconst(ClFunctionBuilder* builder, ClType type, const uint8_t* bytes, size_t len);
ClValue cl_ins_f32const(ClFunctionBuilder* builder, uint32_t bits);
ClValue cl_ins_f64const(ClFunctionBuilder* builder, uint64_t bits);
ClValue cl_ins_symbol_value(ClFunctionBuilder* builder, ClType type, ClGlobalValue global);
ClValue cl_ins_iadd(ClFunctionBuilder* builder, ClValue left, ClValue right);
// add equal-width scalar integers and return the sum followed by an i8 unsigned overflow flag
ClValuePair cl_ins_uadd_overflow(ClFunctionBuilder* builder, ClValue left, ClValue right);
// include an i8 carry input where nonzero means one and return the sum followed by an i8 overflow flag
// the pinned x64 backend currently rejects this instruction during compilation
ClValuePair cl_ins_uadd_overflow_cin(ClFunctionBuilder* builder, ClValue left, ClValue right,
                                     ClValue carry_in);
ClValue cl_ins_isub(ClFunctionBuilder* builder, ClValue left, ClValue right);
ClValue cl_ins_imul(ClFunctionBuilder* builder, ClValue left, ClValue right);
// choose the larger integer using unsigned comparison with matching operand types
ClValue cl_ins_umax(ClFunctionBuilder* builder, ClValue left, ClValue right);
// choose the smaller integer using unsigned comparison with matching operand types
ClValue cl_ins_umin(ClFunctionBuilder* builder, ClValue left, ClValue right);
// choose the larger integer using signed comparison with matching operand types
ClValue cl_ins_smax(ClFunctionBuilder* builder, ClValue left, ClValue right);
// choose the smaller integer using signed comparison with matching operand types
ClValue cl_ins_smin(ClFunctionBuilder* builder, ClValue left, ClValue right);
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
// reinterpret equal-sized types without numerical conversion using flags from this function
// specify endianness when vector lane counts differ and use memflags_new for scalar casts
ClValue cl_ins_bitcast(ClFunctionBuilder* builder, ClType type, ClMemFlags flags, ClValue value);
// replicate a scalar into every lane of the requested vector type whose lane type must match value
ClValue cl_ins_splat(ClFunctionBuilder* builder, ClType type, ClValue value);
// return an i8 boolean that is true when every vector lane is nonzero
ClValue cl_ins_vall_true(ClFunctionBuilder* builder, ClValue vector);
// return an i8 boolean that is true when any vector lane is nonzero
ClValue cl_ins_vany_true(ClFunctionBuilder* builder, ClValue vector);
// extract a scalar lane using a constant index less than the vector lane count
ClValue cl_ins_extractlane(ClFunctionBuilder* builder, ClValue vector, uint8_t lane);
// replace a lane using a matching scalar value and a constant index less than the vector lane count
ClValue cl_ins_insertlane(ClFunctionBuilder* builder, ClValue vector, ClValue value, uint8_t lane);
// shuffle two i8x16 vectors using 16 readable mask bytes copied into the function
// indices 0 through 15 select left and 16 through 31 select right
// return the invalid value and set last_error for a null mask or an index outside 0 through 31
ClValue cl_ins_shuffle(ClFunctionBuilder* builder, ClValue left, ClValue right, const uint8_t* mask);
// permute i8x16 bytes using i8x16 runtime indices with indices above 15 producing zero
ClValue cl_ins_swizzle(ClFunctionBuilder* builder, ClValue vector, ClValue indices);
// copy the sign bit of right onto left without changing the other bits
ClValue cl_ins_fcopysign(ClFunctionBuilder* builder, ClValue left, ClValue right);
// choose the smaller float and propagate nan with negative zero preferred over positive zero
ClValue cl_ins_fmin(ClFunctionBuilder* builder, ClValue left, ClValue right);
// choose the larger float and propagate nan with positive zero preferred over negative zero
ClValue cl_ins_fmax(ClFunctionBuilder* builder, ClValue left, ClValue right);
// compute the floating point square root
ClValue cl_ins_sqrt(ClFunctionBuilder* builder, ClValue value);
// flip the floating point sign bit without changing the other bits
ClValue cl_ins_fneg(ClFunctionBuilder* builder, ClValue value);
// clear the floating point sign bit without changing the other bits
ClValue cl_ins_fabs(ClFunctionBuilder* builder, ClValue value);
// round toward positive infinity and keep the floating point type
ClValue cl_ins_ceil(ClFunctionBuilder* builder, ClValue value);
// round toward negative infinity and keep the floating point type
ClValue cl_ins_floor(ClFunctionBuilder* builder, ClValue value);
// round toward zero and keep the floating point type
ClValue cl_ins_trunc(ClFunctionBuilder* builder, ClValue value);
// round to the nearest integral float with ties to even
ClValue cl_ins_nearest(ClFunctionBuilder* builder, ClValue value);
// convert f64x2 to f32x4 with rounding to nearest ties to even and zero the upper two lanes
ClValue cl_ins_fvdemote(ClFunctionBuilder* builder, ClValue value);
// convert the lower two lanes of f32x4 to f64x2 and discard the upper lanes
ClValue cl_ins_fvpromote_low(ClFunctionBuilder* builder, ClValue value);
// convert a scalar float to a wider float type preserving its numerical value
ClValue cl_ins_fpromote(ClFunctionBuilder* builder, ClType type, ClValue value);
// convert a scalar float to a narrower float type with rounding to nearest ties to even
ClValue cl_ins_fdemote(ClFunctionBuilder* builder, ClType type, ClValue value);
// convert a scalar float to an unsigned integer toward zero and trap on nan or overflow
ClValue cl_ins_fcvt_to_uint(ClFunctionBuilder* builder, ClType type, ClValue value);
// convert a scalar float to a signed integer toward zero and trap on nan or overflow
ClValue cl_ins_fcvt_to_sint(ClFunctionBuilder* builder, ClType type, ClValue value);
// convert float lanes to unsigned integers toward zero with clamping and nan converted to zero
ClValue cl_ins_fcvt_to_uint_sat(ClFunctionBuilder* builder, ClType type, ClValue value);
// convert float lanes to signed integers toward zero with clamping and nan converted to zero
ClValue cl_ins_fcvt_to_sint_sat(ClFunctionBuilder* builder, ClType type, ClValue value);
// convert unsigned integer lanes to floats with rounding to nearest ties to even
ClValue cl_ins_fcvt_from_uint(ClFunctionBuilder* builder, ClType type, ClValue value);
// convert signed integer lanes to floats with rounding to nearest ties to even
ClValue cl_ins_fcvt_from_sint(ClFunctionBuilder* builder, ClType type, ClValue value);
// compute left times right plus addend with a single rounding and matching float types
ClValue cl_ins_fma(ClFunctionBuilder* builder, ClValue left, ClValue right, ClValue addend);
// compare matching float types with an i8 scalar result or a lane mask for vectors
// not_equal includes nan while ordered_not_equal excludes nan
ClValue cl_ins_fcmp(ClFunctionBuilder* builder, uint32_t condition, ClValue left, ClValue right);

ClValue cl_ins_ineg(ClFunctionBuilder* builder, ClValue value);
ClValue cl_ins_bnot(ClFunctionBuilder* builder, ClValue value);
// count leading zero bits and return the input bit width for zero
ClValue cl_ins_clz(ClFunctionBuilder* builder, ClValue value);
// count trailing zero bits and return the input bit width for zero
ClValue cl_ins_ctz(ClFunctionBuilder* builder, ClValue value);
// count set bits in the integer value
ClValue cl_ins_popcnt(ClFunctionBuilder* builder, ClValue value);
ClValue cl_ins_bswap(ClFunctionBuilder* builder, ClValue value);
ClValue cl_ins_ireduce(ClFunctionBuilder* builder, ClType type, ClValue value);
ClValue cl_ins_uextend(ClFunctionBuilder* builder, ClType type, ClValue value);
ClValue cl_ins_sextend(ClFunctionBuilder* builder, ClType type, ClValue value);
ClValue cl_ins_iadd_imm(ClFunctionBuilder* builder, ClValue value, int64_t immediate);
// add an immediate to an integer with a sign-extended immediate
ClValue cl_ins_iadd_imm_s(ClFunctionBuilder* builder, ClValue value, int64_t immediate);
// add an immediate to an integer with a zero-extended immediate
ClValue cl_ins_iadd_imm_u(ClFunctionBuilder* builder, ClValue value, uint64_t immediate);
// multiply an integer by an immediate with a sign-extended immediate
ClValue cl_ins_imul_imm_s(ClFunctionBuilder* builder, ClValue value, int64_t immediate);
// multiply an integer by an immediate with a zero-extended immediate
ClValue cl_ins_imul_imm_u(ClFunctionBuilder* builder, ClValue value, uint64_t immediate);
// divide an unsigned integer by an immediate with a sign-extended immediate
ClValue cl_ins_udiv_imm_s(ClFunctionBuilder* builder, ClValue value, int64_t immediate);
// divide an unsigned integer by an immediate with a zero-extended immediate
ClValue cl_ins_udiv_imm_u(ClFunctionBuilder* builder, ClValue value, uint64_t immediate);
// divide a signed integer by an immediate with a sign-extended immediate
ClValue cl_ins_sdiv_imm_s(ClFunctionBuilder* builder, ClValue value, int64_t immediate);
// divide a signed integer by an immediate with a zero-extended immediate
ClValue cl_ins_sdiv_imm_u(ClFunctionBuilder* builder, ClValue value, uint64_t immediate);
// compute the unsigned remainder with an immediate divisor with a sign-extended immediate
ClValue cl_ins_urem_imm_s(ClFunctionBuilder* builder, ClValue value, int64_t immediate);
// compute the unsigned remainder with an immediate divisor with a zero-extended immediate
ClValue cl_ins_urem_imm_u(ClFunctionBuilder* builder, ClValue value, uint64_t immediate);
// compute the signed remainder with an immediate divisor with a sign-extended immediate
ClValue cl_ins_srem_imm_s(ClFunctionBuilder* builder, ClValue value, int64_t immediate);
// compute the signed remainder with an immediate divisor with a zero-extended immediate
ClValue cl_ins_srem_imm_u(ClFunctionBuilder* builder, ClValue value, uint64_t immediate);
// apply bitwise and with an immediate with a sign-extended immediate
ClValue cl_ins_band_imm_s(ClFunctionBuilder* builder, ClValue value, int64_t immediate);
// apply bitwise and with an immediate with a zero-extended immediate
ClValue cl_ins_band_imm_u(ClFunctionBuilder* builder, ClValue value, uint64_t immediate);
// apply bitwise or with an immediate with a sign-extended immediate
ClValue cl_ins_bor_imm_s(ClFunctionBuilder* builder, ClValue value, int64_t immediate);
// apply bitwise or with an immediate with a zero-extended immediate
ClValue cl_ins_bor_imm_u(ClFunctionBuilder* builder, ClValue value, uint64_t immediate);
// apply bitwise xor with an immediate with a sign-extended immediate
ClValue cl_ins_bxor_imm_s(ClFunctionBuilder* builder, ClValue value, int64_t immediate);
// apply bitwise xor with an immediate with a zero-extended immediate
ClValue cl_ins_bxor_imm_u(ClFunctionBuilder* builder, ClValue value, uint64_t immediate);
// rotate an integer left by an immediate with a sign-extended immediate
ClValue cl_ins_rotl_imm_s(ClFunctionBuilder* builder, ClValue value, int64_t immediate);
// rotate an integer left by an immediate with a zero-extended immediate
ClValue cl_ins_rotl_imm_u(ClFunctionBuilder* builder, ClValue value, uint64_t immediate);
// rotate an integer right by an immediate with a sign-extended immediate
ClValue cl_ins_rotr_imm_s(ClFunctionBuilder* builder, ClValue value, int64_t immediate);
// rotate an integer right by an immediate with a zero-extended immediate
ClValue cl_ins_rotr_imm_u(ClFunctionBuilder* builder, ClValue value, uint64_t immediate);
// shift an integer left by an immediate with a sign-extended immediate
ClValue cl_ins_ishl_imm_s(ClFunctionBuilder* builder, ClValue value, int64_t immediate);
// shift an integer left by an immediate with a zero-extended immediate
ClValue cl_ins_ishl_imm_u(ClFunctionBuilder* builder, ClValue value, uint64_t immediate);
// shift an integer right without sign extension by an immediate with a sign-extended immediate
ClValue cl_ins_ushr_imm_s(ClFunctionBuilder* builder, ClValue value, int64_t immediate);
// shift an integer right without sign extension by an immediate with a zero-extended immediate
ClValue cl_ins_ushr_imm_u(ClFunctionBuilder* builder, ClValue value, uint64_t immediate);
// shift an integer right with sign extension by an immediate with a sign-extended immediate
ClValue cl_ins_sshr_imm_s(ClFunctionBuilder* builder, ClValue value, int64_t immediate);
// shift an integer right with sign extension by an immediate with a zero-extended immediate
ClValue cl_ins_sshr_imm_u(ClFunctionBuilder* builder, ClValue value, uint64_t immediate);
// compare with a sign-extended immediate and return an i8 result for scalar inputs
ClValue cl_ins_icmp_imm_s(ClFunctionBuilder* builder, uint32_t condition, ClValue value, int64_t immediate);
// compare with a zero-extended immediate and return an i8 result for scalar inputs
ClValue cl_ins_icmp_imm_u(ClFunctionBuilder* builder, uint32_t condition, ClValue value, uint64_t immediate);
// compare with a signed immediate using the historical helper behavior
ClValue cl_ins_icmp_imm(ClFunctionBuilder* builder, uint32_t condition, ClValue value, int64_t immediate);
ClValue cl_ins_icmp(ClFunctionBuilder* builder, uint32_t condition, ClValue left, ClValue right);
ClValue cl_ins_select(ClFunctionBuilder* builder, ClValue condition, ClValue if_true, ClValue if_false);
// terminate the current block with a native trap or return an invalid id for a zero trap code
ClInst cl_ins_trap(ClFunctionBuilder* builder, ClTrapCode code);
// trap when the scalar integer condition is zero and continue normally otherwise
ClInst cl_ins_trapz(ClFunctionBuilder* builder, ClValue condition, ClTrapCode code);
// trap when the scalar integer condition is nonzero and continue normally otherwise
ClInst cl_ins_trapnz(ClFunctionBuilder* builder, ClValue condition, ClTrapCode code);
// emit a native debugger breakpoint without terminating the block
ClInst cl_ins_debugtrap(ClFunctionBuilder* builder);
// add matching unsigned scalar integers and trap if the sum overflows
ClValue cl_ins_uadd_overflow_trap(ClFunctionBuilder* builder, ClValue left, ClValue right, ClTrapCode code);
// select bits as mask and if_true or inverted mask and if_false with all operands of the same type
ClValue cl_ins_bitselect(ClFunctionBuilder* builder, ClValue mask, ClValue if_true, ClValue if_false);
ClInst cl_ins_jump(ClFunctionBuilder* builder, ClBlock destination, const ClValue* args, size_t len);
ClInst cl_ins_brif(ClFunctionBuilder* builder, ClValue condition, ClBlock then_block,
                   const ClValue* then_args, size_t then_len, ClBlock else_block, const ClValue* else_args,
                   size_t else_len);
ClInst cl_ins_nop(ClFunctionBuilder* builder);
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

// emit a sequentially consistent fence that orders all loads and stores across it
ClInst cl_ins_fence(ClFunctionBuilder* builder);
// atomically load an integer with sequentially consistent ordering
ClValue cl_ins_atomic_load(ClFunctionBuilder* builder, ClType type, ClMemFlags flags, ClValue address);
// atomically store an integer with sequentially consistent ordering
ClInst cl_ins_atomic_store(ClFunctionBuilder* builder, ClMemFlags flags, ClValue value, ClValue address);
// atomically apply an operation and return the old integer with sequentially consistent ordering
ClValue cl_ins_atomic_rmw(ClFunctionBuilder* builder, ClType type, ClMemFlags flags, uint32_t operation,
                         ClValue address, ClValue value);
// store replacement if memory equals expected and return the old integer whether or not it matched
ClValue cl_ins_atomic_cas(ClFunctionBuilder* builder, ClMemFlags flags, ClValue address,
                         ClValue expected, ClValue replacement);

#ifdef __cplusplus
}
}  // namespace cranelift
#endif
