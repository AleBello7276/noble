//! Direct C entry points for the Cranelift 0.135.2 APIs used by the C++ frontend.
//! Rust-owned objects stay opaque; Cranelift entity IDs cross as their u32 indexes.

use std::cell::RefCell;
use std::ffi::{c_char, c_void, CStr, CString};
use std::ptr;
use std::num::NonZeroU8;

use cranelift_codegen::entity::EntityRef;
use cranelift_codegen::ir::{condcodes::{FloatCC, IntCC}, types, AbiParam, Block, BlockArg, Endianness, FuncRef, GlobalValue, Inst, InstBuilder, MemFlags, MemFlagsData, SigRef, Signature, Type, Value};
use cranelift_codegen::isa::{self, OwnedTargetIsa};
use cranelift_codegen::{settings, Context};
use cranelift_frontend::{FunctionBuilder, FunctionBuilderContext, Variable};
use cranelift_jit::{JITBuilder, JITModule};
use cranelift_module::{default_libcall_names, DataDescription, DataId, FuncId, Linkage, Module};

const INVALID: u32 = u32::MAX;

// decode a nonzero raw trap code without panicking across the c abi
fn trapcode(code: u8) -> Option<cranelift_codegen::ir::TrapCode> {
    match NonZeroU8::new(code) {
        Some(code) => Some(cranelift_codegen::ir::TrapCode::from_raw(code)),
        None => { error("invalid zero trap code"); None }
    }
}

// construct a user trap code and return zero for reserved or invalid codes
#[no_mangle]
pub extern "C" fn cl_trapcode_user(code: u8) -> u8 {
    match cranelift_codegen::ir::TrapCode::user(code) {
        Some(code) => code.as_raw().get(),
        None => { error("user trap code must be between 1 and 250"); 0 }
    }
}

// preserve the order of two cranelift instruction results across the c abi
#[repr(C)]
pub struct ClValuePair {
    first: u32,
    second: u32,
}

thread_local! {
    static LAST_ERROR: RefCell<CString> = RefCell::new(CString::new("").unwrap());
}

fn error(message: impl std::fmt::Display) {
    LAST_ERROR.with(|slot| *slot.borrow_mut() = CString::new(message.to_string()).unwrap_or_default());
}

unsafe fn string<'a>(value: *const c_char) -> Option<&'a str> {
    if value.is_null() {
        error("null string");
        return None;
    }
    match CStr::from_ptr(value).to_str() {
        Ok(value) => Some(value),
        Err(e) => { error(e); None }
    }
}

unsafe fn values<'a>(value: *const u32, len: usize) -> Option<Vec<Value>> {
    if len != 0 && value.is_null() {
        error("null value array");
        return None;
    }
    let ids = if len == 0 { &[][..] } else { std::slice::from_raw_parts(value, len) };
    Some(ids.iter().copied().map(Value::from_u32).collect())
}

fn ty(raw: u16) -> Type {
    // Type is a u16 newtype in the pinned Cranelift release. The public
    // cl_type_* functions supply valid values; callers must pass those values.
    unsafe { std::mem::transmute(raw) }
}

fn raw_type(value: Type) -> u16 { unsafe { std::mem::transmute(value) } }

#[no_mangle]
pub extern "C" fn cl_last_error() -> *const c_char {
    LAST_ERROR.with(|slot| slot.borrow().as_ptr())
}

macro_rules! scalar_type {
    ($name:ident, $value:expr) => {
        #[no_mangle]
        pub extern "C" fn $name() -> u16 { raw_type($value) }
    };
}
scalar_type!(cl_type_i8, types::I8);
scalar_type!(cl_type_i16, types::I16);
scalar_type!(cl_type_i32, types::I32);
scalar_type!(cl_type_i64, types::I64);
scalar_type!(cl_type_i128, types::I128);
scalar_type!(cl_type_f16, types::F16);
scalar_type!(cl_type_f32, types::F32);
scalar_type!(cl_type_f64, types::F64);
scalar_type!(cl_type_f128, types::F128);

#[no_mangle]
pub extern "C" fn cl_type_int(bits: u16) -> u16 {
    match Type::int(bits) {
        Some(value) => raw_type(value),
        None => { error("unsupported integer width"); 0 }
    }
}

#[no_mangle]
pub extern "C" fn cl_type_vector(lane_type: u16, lanes: u32) -> u16 {
    let lane = ty(lane_type);
    if !lane.is_int() && !lane.is_float() {
        error("vector lane must be a scalar integer or float type");
        return 0;
    }
    match lane.by(lanes) {
        Some(value) if value.is_vector() => raw_type(value),
        _ => { error("unsupported vector lane count"); 0 }
    }
}

#[no_mangle]
pub extern "C" fn cl_type_vector_to_dynamic(fixed_vector_type: u16) -> u16 {
    let fixed = ty(fixed_vector_type);
    if !fixed.is_vector() {
        error("type must be a fixed vector");
        return 0;
    }
    match fixed.vector_to_dynamic() {
        Some(value) => raw_type(value),
        None => { error("dynamic vector must be at most 256 bits"); 0 }
    }
}

#[no_mangle]
pub extern "C" fn cl_settings_builder_new() -> *mut settings::Builder {
    Box::into_raw(Box::new(settings::builder()))
}

#[no_mangle]
pub unsafe extern "C" fn cl_settings_builder_drop(builder: *mut settings::Builder) {
    if !builder.is_null() { drop(Box::from_raw(builder)); }
}

#[no_mangle]
pub unsafe extern "C" fn cl_settings_set(builder: *mut settings::Builder, name: *const c_char, value: *const c_char) -> bool {
    use settings::Configurable;
    let (Some(name), Some(value)) = (string(name), string(value)) else { return false };
    match (*builder).set(name, value) {
        Ok(()) => true,
        Err(e) => { error(e); false }
    }
}

#[no_mangle]
pub unsafe extern "C" fn cl_settings_enable(builder: *mut settings::Builder, name: *const c_char) -> bool {
    use settings::Configurable;
    let Some(name) = string(name) else { return false };
    match (*builder).enable(name) {
        Ok(()) => true,
        Err(e) => { error(e); false }
    }
}

#[no_mangle]
pub extern "C" fn cl_native_builder() -> *mut isa::Builder {
    cl_native_builder_with_options(true)
}

#[no_mangle]
pub extern "C" fn cl_native_builder_with_options(infer_native_flags: bool) -> *mut isa::Builder {
    match cranelift_native::builder_with_options(infer_native_flags) {
        Ok(builder) => Box::into_raw(Box::new(builder)),
        Err(e) => { error(e); ptr::null_mut() }
    }
}

#[no_mangle]
pub unsafe extern "C" fn cl_native_builder_drop(builder: *mut isa::Builder) {
    if !builder.is_null() { drop(Box::from_raw(builder)); }
}

#[no_mangle]
pub unsafe extern "C" fn cl_native_set(builder: *mut isa::Builder, name: *const c_char, value: *const c_char) -> bool {
    use settings::Configurable;
    let (Some(name), Some(value)) = (string(name), string(value)) else { return false };
    match (*builder).set(name, value) {
        Ok(()) => true, Err(e) => { error(e); false }
    }
}

#[no_mangle]
pub unsafe extern "C" fn cl_native_enable(builder: *mut isa::Builder, name: *const c_char) -> bool {
    use settings::Configurable;
    let Some(name) = string(name) else { return false };
    match (*builder).enable(name) {
        Ok(()) => true, Err(e) => { error(e); false }
    }
}

#[no_mangle]
pub unsafe extern "C" fn cl_isa_finish(builder: *mut isa::Builder, flags: *mut settings::Builder) -> *mut OwnedTargetIsa {
    let flags = settings::Flags::new(*Box::from_raw(flags));
    match Box::from_raw(builder).finish(flags) {
        Ok(isa) => Box::into_raw(Box::new(isa)),
        Err(e) => { error(e); ptr::null_mut() }
    }
}

#[no_mangle]
pub unsafe extern "C" fn cl_isa_drop(isa: *mut OwnedTargetIsa) {
    if !isa.is_null() { drop(Box::from_raw(isa)); }
}

#[no_mangle]
pub extern "C" fn cl_jit_builder_new() -> *mut JITBuilder {
    match JITBuilder::new(default_libcall_names()) {
        Ok(builder) => Box::into_raw(Box::new(builder)),
        Err(e) => { error(e); ptr::null_mut() }
    }
}

#[no_mangle]
pub unsafe extern "C" fn cl_jit_builder_with_flags(names: *const *const c_char, values: *const *const c_char, len: usize) -> *mut JITBuilder {
    if len != 0 && (names.is_null() || values.is_null()) {
        error("null flag array");
        return ptr::null_mut();
    }
    let names = if len == 0 { &[][..] } else { std::slice::from_raw_parts(names, len) };
    let values = if len == 0 { &[][..] } else { std::slice::from_raw_parts(values, len) };
    let mut flags = Vec::with_capacity(len);
    for (&name, &value) in names.iter().zip(values) {
        let (Some(name), Some(value)) = (string(name), string(value)) else { return ptr::null_mut() };
        flags.push((name, value));
    }
    match JITBuilder::with_flags(&flags, default_libcall_names()) {
        Ok(builder) => Box::into_raw(Box::new(builder)),
        Err(e) => { error(e); ptr::null_mut() }
    }
}

#[no_mangle]
pub unsafe extern "C" fn cl_jit_builder_with_isa(isa: *mut OwnedTargetIsa) -> *mut JITBuilder {
    Box::into_raw(Box::new(JITBuilder::with_isa(*Box::from_raw(isa), default_libcall_names())))
}

#[no_mangle]
pub unsafe extern "C" fn cl_jit_builder_symbol(builder: *mut JITBuilder, name: *const c_char, address: *const c_void) -> bool {
    let Some(name) = string(name) else { return false };
    (*builder).symbol(name, address.cast());
    true
}

#[no_mangle]
pub unsafe extern "C" fn cl_jit_builder_drop(builder: *mut JITBuilder) {
    if !builder.is_null() { drop(Box::from_raw(builder)); }
}

#[no_mangle]
pub unsafe extern "C" fn cl_jit_module_new(builder: *mut JITBuilder) -> *mut JITModule {
    Box::into_raw(Box::new(JITModule::new(*Box::from_raw(builder))))
}

#[no_mangle]
pub unsafe extern "C" fn cl_jit_module_drop(module: *mut JITModule) {
    if !module.is_null() { drop(Box::from_raw(module)); }
}

#[no_mangle]
pub unsafe extern "C" fn cl_jit_module_free_memory(module: *mut JITModule) {
    if !module.is_null() { Box::from_raw(module).free_memory(); }
}

#[no_mangle]
pub unsafe extern "C" fn cl_module_make_context(module: *const JITModule) -> *mut Context {
    Box::into_raw(Box::new((*module).make_context()))
}

#[no_mangle]
pub unsafe extern "C" fn cl_module_pointer_type(module: *const JITModule) -> u16 {
    raw_type((*module).target_config().pointer_type())
}

#[no_mangle]
pub unsafe extern "C" fn cl_module_clear_context(module: *const JITModule, context: *mut Context) {
    (*module).clear_context(&mut *context);
}

#[no_mangle]
pub unsafe extern "C" fn cl_context_drop(context: *mut Context) {
    if !context.is_null() { drop(Box::from_raw(context)); }
}

#[no_mangle]
pub unsafe extern "C" fn cl_context_verify(context: *const Context, module: *const JITModule) -> bool {
    match (*context).verify((*module).isa()) {
        Ok(()) => true,
        Err(e) => { error(e); false }
    }
}

#[no_mangle]
pub unsafe extern "C" fn cl_context_display(context: *const Context, buffer: *mut c_char, capacity: usize) -> usize {
    cl_context_display_with_comments(context, ptr::null(), 0, buffer, capacity)
}

#[repr(C)]
pub struct IRComment {
    instruction: u32,
    block: u32,
    text: *const c_char,
}

#[no_mangle]
pub unsafe extern "C" fn cl_builder_comment_position(builder: *const FunctionBuilder<'static>,
    block: *mut u32, instruction: *mut u32) -> bool {
    if builder.is_null() || block.is_null() || instruction.is_null() {
        error("null comment position argument");
        return false;
    }
    let builder = &*builder;
    let Some(current) = builder.current_block() else {
        error("comment requires an active block");
        return false;
    };
    *block = current.as_u32();
    *instruction = builder.func.layout.last_inst(current).map_or(INVALID, |inst| inst.as_u32());
    true
}

struct CommentWriter<'a> {
    comments: &'a [IRComment],
}

impl CommentWriter<'_> {
    fn write_comments(&self, w: &mut dyn std::fmt::Write, instruction: u32,
        block: u32, indent: usize) -> std::fmt::Result {
        for comment in self.comments.iter().filter(|c| c.instruction == instruction && c.block == block) {
            let text = unsafe { CStr::from_ptr(comment.text) }.to_string_lossy();
            for line in text.lines() {
                writeln!(w, "{:indent$}; {}", "", line, indent = indent)?;
            }
        }
        Ok(())
    }
}

impl cranelift_codegen::write::FuncWriter for CommentWriter<'_> {
    fn write_block_header(&mut self, w: &mut dyn std::fmt::Write,
        func: &cranelift_codegen::ir::Function, block: Block, indent: usize) -> std::fmt::Result {
        cranelift_codegen::write::write_block_header(w, func, block, indent)?;
        self.write_comments(w, INVALID, block.as_u32(), indent + 4)
    }

    fn write_instruction(&mut self, w: &mut dyn std::fmt::Write,
        func: &cranelift_codegen::ir::Function,
        aliases: &cranelift_codegen::entity::SecondaryMap<Value, Vec<Value>>,
        inst: Inst, indent: usize) -> std::fmt::Result {
        self.write_comments(w, inst.as_u32(), INVALID, indent)?;
        cranelift_codegen::write::FuncWriter::write_instruction(
            &mut cranelift_codegen::write::PlainWriter, w, func, aliases, inst, indent)?;
        self.write_comments(w, inst.as_u32(), func.layout.inst_block(inst).unwrap().as_u32(), indent)
    }
}

#[no_mangle]
pub unsafe extern "C" fn cl_context_display_with_comments(context: *const Context,
    comments: *const IRComment, count: usize, buffer: *mut c_char, capacity: usize) -> usize {
    if context.is_null() || (capacity != 0 && buffer.is_null()) {
        error("null context or output buffer");
        return 0;
    }
    if count != 0 && comments.is_null() {
        error("null comment array");
        return 0;
    }
    let comments = if count == 0 { &[][..] } else { std::slice::from_raw_parts(comments, count) };
    if comments.iter().any(|c| c.text.is_null()) {
        error("null comment text");
        return 0;
    }
    let mut text = String::new();
    if cranelift_codegen::write::decorate_function(&mut CommentWriter { comments }, &mut text, &(*context).func).is_err() {
        error("failed to write CLIR");
        return 0;
    }
    let bytes = text.as_bytes();
    if capacity != 0 {
        let count = bytes.len().min(capacity - 1);
        ptr::copy_nonoverlapping(bytes.as_ptr(), buffer.cast(), count);
        *buffer.add(count) = 0;
    }
    bytes.len() + 1
}

#[no_mangle]
pub unsafe extern "C" fn cl_context_signature(context: *mut Context) -> *mut Signature {
    &mut (&mut *context).func.signature
}

#[no_mangle]
pub unsafe extern "C" fn cl_module_make_signature(module: *const JITModule) -> *mut Signature {
    Box::into_raw(Box::new((*module).make_signature()))
}

#[no_mangle]
pub unsafe extern "C" fn cl_module_clear_signature(module: *const JITModule, signature: *mut Signature) {
    (*module).clear_signature(&mut *signature);
}

#[no_mangle]
pub unsafe extern "C" fn cl_signature_drop(signature: *mut Signature) {
    if !signature.is_null() { drop(Box::from_raw(signature)); }
}

#[no_mangle]
pub unsafe extern "C" fn cl_signature_push_param(signature: *mut Signature, value_type: u16) {
    (*signature).params.push(AbiParam::new(ty(value_type)));
}

#[no_mangle]
pub unsafe extern "C" fn cl_signature_push_return(signature: *mut Signature, value_type: u16) {
    (*signature).returns.push(AbiParam::new(ty(value_type)));
}

#[no_mangle]
pub unsafe extern "C" fn cl_signature_set_call_conv(signature: *mut Signature, convention: u32) -> bool {
    use isa::CallConv;
    (*signature).call_conv = match convention {
        0 => CallConv::Fast, 1 => CallConv::Tail, 2 => CallConv::SystemV,
        3 => CallConv::WindowsFastcall, 4 => CallConv::AppleAarch64,
        5 => CallConv::Probestack, 6 => CallConv::Winch, 7 => CallConv::PreserveAll,
        _ => { error("invalid CallConv"); return false }
    };
    true
}

fn linkage(value: u32) -> Option<Linkage> {
    match value {
        0 => Some(Linkage::Import), 1 => Some(Linkage::Local),
        2 => Some(Linkage::Preemptible), 3 => Some(Linkage::Hidden),
        4 => Some(Linkage::Export), _ => { error("invalid Linkage"); None }
    }
}

#[no_mangle]
pub unsafe extern "C" fn cl_module_declare_function(module: *mut JITModule, name: *const c_char, link: u32, signature: *const Signature) -> u32 {
    let (Some(name), Some(link)) = (string(name), linkage(link)) else { return INVALID };
    match (*module).declare_function(name, link, &*signature) {
        Ok(id) => id.as_u32(), Err(e) => { error(e); INVALID }
    }
}

#[no_mangle]
pub unsafe extern "C" fn cl_module_declare_func_in_func(module: *mut JITModule, id: u32, context: *mut Context) -> u32 {
    (*module).declare_func_in_func(FuncId::from_u32(id), &mut (*context).func).as_u32()
}

#[no_mangle]
pub unsafe extern "C" fn cl_module_define_function(module: *mut JITModule, id: u32, context: *mut Context) -> bool {
    match (*module).define_function(FuncId::from_u32(id), &mut *context) {
        Ok(()) => true, Err(e) => { error(e); false }
    }
}

#[no_mangle]
pub unsafe extern "C" fn cl_jit_module_finalize_definitions(module: *mut JITModule) -> bool {
    match (*module).finalize_definitions() {
        Ok(()) => true, Err(e) => { error(e); false }
    }
}

#[no_mangle]
pub unsafe extern "C" fn cl_jit_module_get_finalized_function(module: *const JITModule, id: u32) -> *const c_void {
    (*module).get_finalized_function(FuncId::from_u32(id)).cast()
}

#[no_mangle]
pub extern "C" fn cl_data_description_new() -> *mut DataDescription {
    Box::into_raw(Box::new(DataDescription::new()))
}

#[no_mangle]
pub unsafe extern "C" fn cl_data_description_drop(description: *mut DataDescription) {
    if !description.is_null() { drop(Box::from_raw(description)); }
}

#[no_mangle]
pub unsafe extern "C" fn cl_data_description_clear(description: *mut DataDescription) {
    (*description).clear();
}

#[no_mangle]
pub unsafe extern "C" fn cl_data_description_define_zeroinit(description: *mut DataDescription, size: usize) {
    (*description).define_zeroinit(size);
}

#[no_mangle]
pub unsafe extern "C" fn cl_data_description_define(description: *mut DataDescription, bytes: *const u8, size: usize) -> bool {
    if size != 0 && bytes.is_null() { error("null data bytes"); return false }
    let bytes = if size == 0 { &[][..] } else { std::slice::from_raw_parts(bytes, size) };
    (*description).define(bytes.into());
    true
}

#[no_mangle]
pub unsafe extern "C" fn cl_data_description_set_align(description: *mut DataDescription, align: u64) -> bool {
    if !align.is_power_of_two() { error("alignment must be a power of two"); return false }
    (*description).set_align(align);
    true
}

#[no_mangle]
pub unsafe extern "C" fn cl_module_declare_data(module: *mut JITModule, name: *const c_char, link: u32, writable: bool, tls: bool) -> u32 {
    let (Some(name), Some(link)) = (string(name), linkage(link)) else { return INVALID };
    match (*module).declare_data(name, link, writable, tls) {
        Ok(id) => id.as_u32(), Err(e) => { error(e); INVALID }
    }
}

#[no_mangle]
pub unsafe extern "C" fn cl_module_define_data(module: *mut JITModule, id: u32, description: *const DataDescription) -> bool {
    match (*module).define_data(DataId::from_u32(id), &*description) {
        Ok(()) => true, Err(e) => { error(e); false }
    }
}

#[no_mangle]
pub unsafe extern "C" fn cl_module_declare_data_in_func(module: *const JITModule, id: u32, context: *mut Context) -> u32 {
    (*module).declare_data_in_func(DataId::from_u32(id), &mut (*context).func).as_u32()
}

#[no_mangle]
pub unsafe extern "C" fn cl_jit_module_get_finalized_data(module: *const JITModule, id: u32, size: *mut usize) -> *const c_void {
    let (address, length) = (*module).get_finalized_data(DataId::from_u32(id));
    if !size.is_null() { *size = length; }
    address.cast()
}

#[no_mangle]
pub extern "C" fn cl_function_builder_context_new() -> *mut FunctionBuilderContext {
    Box::into_raw(Box::new(FunctionBuilderContext::new()))
}

#[no_mangle]
pub unsafe extern "C" fn cl_function_builder_context_drop(context: *mut FunctionBuilderContext) {
    if !context.is_null() { drop(Box::from_raw(context)); }
}

#[no_mangle]
pub unsafe extern "C" fn cl_function_builder_new(context: *mut Context, builder_context: *mut FunctionBuilderContext) -> *mut FunctionBuilder<'static> {
    // Raw pointers carry the lifetime contract across C: both contexts must
    // remain alive and unused elsewhere until finish consumes this builder.
    Box::into_raw(Box::new(FunctionBuilder::new(&mut (*context).func, &mut *builder_context)))
}

#[no_mangle]
pub unsafe extern "C" fn cl_function_builder_drop(builder: *mut FunctionBuilder<'static>) {
    if !builder.is_null() { drop(Box::from_raw(builder)); }
}

#[no_mangle]
pub unsafe extern "C" fn cl_function_builder_finish(builder: *mut FunctionBuilder<'static>, module: *const JITModule) {
    Box::from_raw(builder).finalize((*module).target_config());
}

#[no_mangle]
pub unsafe extern "C" fn cl_builder_create_block(builder: *mut FunctionBuilder<'static>) -> u32 {
    (*builder).create_block().as_u32()
}

#[no_mangle]
pub unsafe extern "C" fn cl_builder_switch_to_block(builder: *mut FunctionBuilder<'static>, block: u32) {
    (*builder).switch_to_block(Block::from_u32(block));
}

#[no_mangle]
pub unsafe extern "C" fn cl_builder_seal_block(builder: *mut FunctionBuilder<'static>, block: u32) {
    (*builder).seal_block(Block::from_u32(block));
}

#[no_mangle]
pub unsafe extern "C" fn cl_builder_seal_all_blocks(builder: *mut FunctionBuilder<'static>) {
    (*builder).seal_all_blocks();
}

#[no_mangle]
pub unsafe extern "C" fn cl_builder_append_block_params_for_function_params(builder: *mut FunctionBuilder<'static>, block: u32) {
    (*builder).append_block_params_for_function_params(Block::from_u32(block));
}

#[no_mangle]
pub unsafe extern "C" fn cl_builder_append_block_param(builder: *mut FunctionBuilder<'static>, block: u32, value_type: u16) -> u32 {
    (*builder).append_block_param(Block::from_u32(block), ty(value_type)).as_u32()
}

#[no_mangle]
pub unsafe extern "C" fn cl_builder_block_param(builder: *const FunctionBuilder<'static>, block: u32, index: usize) -> u32 {
    (*builder).block_params(Block::from_u32(block)).get(index).map_or(INVALID, |v| v.as_u32())
}

#[no_mangle]
pub unsafe extern "C" fn cl_builder_declare_var(builder: *mut FunctionBuilder<'static>, value_type: u16) -> u32 {
    (*builder).declare_var(ty(value_type)).as_u32()
}

#[no_mangle]
pub unsafe extern "C" fn cl_builder_def_var(builder: *mut FunctionBuilder<'static>, variable: u32, value: u32) {
    (*builder).def_var(Variable::from_u32(variable), Value::from_u32(value));
}

#[no_mangle]
pub unsafe extern "C" fn cl_builder_use_var(builder: *mut FunctionBuilder<'static>, variable: u32) -> u32 {
    (*builder).use_var(Variable::from_u32(variable)).as_u32()
}

#[no_mangle]
pub unsafe extern "C" fn cl_builder_inst_result(builder: *const FunctionBuilder<'static>, instruction: u32, index: usize) -> u32 {
    (*builder).inst_results(Inst::from_u32(instruction)).get(index).map_or(INVALID, |v| v.as_u32())
}

#[no_mangle]
pub unsafe extern "C" fn cl_builder_import_signature(builder: *mut FunctionBuilder<'static>, signature: *const Signature) -> u32 {
    (*builder).import_signature((*signature).clone()).as_u32()
}

#[no_mangle]
pub unsafe extern "C" fn cl_builder_declare_func_in_func(module: *mut JITModule, id: u32, builder: *mut FunctionBuilder<'static>) -> u32 {
    let builder = &mut *builder;
    (*module).declare_func_in_func(FuncId::from_u32(id), builder.func).as_u32()
}

#[no_mangle]
pub unsafe extern "C" fn cl_builder_declare_data_in_func(module: *const JITModule, id: u32, builder: *mut FunctionBuilder<'static>) -> u32 {
    let builder = &mut *builder;
    (*module).declare_data_in_func(DataId::from_u32(id), builder.func).as_u32()
}

#[no_mangle]
pub unsafe extern "C" fn cl_ins_iconst(builder: *mut FunctionBuilder<'static>, value_type: u16, immediate: i64) -> u32 {
    (*builder).ins().iconst(ty(value_type), immediate).as_u32()
}

macro_rules! binary_ins {
    ($name:ident, $method:ident) => {
        #[no_mangle]
        pub unsafe extern "C" fn $name(builder: *mut FunctionBuilder<'static>, left: u32, right: u32) -> u32 {
            (*builder).ins().$method(Value::from_u32(left), Value::from_u32(right)).as_u32()
        }
    };
}
binary_ins!(cl_ins_iadd, iadd);

// add equal-width scalar integers and return the sum followed by the i8 unsigned overflow flag
#[no_mangle]
pub unsafe extern "C" fn cl_ins_uadd_overflow(builder: *mut FunctionBuilder<'static>, left: u32, right: u32) -> ClValuePair {
    let (sum, overflow) = (*builder).ins().uadd_overflow(Value::from_u32(left), Value::from_u32(right));
    ClValuePair { first: sum.as_u32(), second: overflow.as_u32() }
}

// add scalar integers with an i8 carry input where any nonzero input represents one
#[no_mangle]
pub unsafe extern "C" fn cl_ins_uadd_overflow_cin(builder: *mut FunctionBuilder<'static>, left: u32, right: u32, carry_in: u32) -> ClValuePair {
    let (sum, overflow) = (*builder).ins().uadd_overflow_cin(Value::from_u32(left), Value::from_u32(right), Value::from_u32(carry_in));
    ClValuePair { first: sum.as_u32(), second: overflow.as_u32() }
}

binary_ins!(cl_ins_isub, isub);
binary_ins!(cl_ins_imul, imul);
// choose the larger integer using unsigned comparison
binary_ins!(cl_ins_umax, umax);
// choose the smaller integer using unsigned comparison
binary_ins!(cl_ins_umin, umin);
// choose the larger integer using signed comparison
binary_ins!(cl_ins_smax, smax);
// choose the smaller integer using signed comparison
binary_ins!(cl_ins_smin, smin);
binary_ins!(cl_ins_band, band);
binary_ins!(cl_ins_bor, bor);
binary_ins!(cl_ins_bxor, bxor);
binary_ins!(cl_ins_ishl, ishl);
binary_ins!(cl_ins_ushr, ushr);
binary_ins!(cl_ins_sshr, sshr);
binary_ins!(cl_ins_udiv, udiv);
binary_ins!(cl_ins_sdiv, sdiv);
binary_ins!(cl_ins_urem, urem);
binary_ins!(cl_ins_srem, srem);
binary_ins!(cl_ins_rotl, rotl);
binary_ins!(cl_ins_rotr, rotr);
binary_ins!(cl_ins_fadd, fadd);
binary_ins!(cl_ins_fsub, fsub);
binary_ins!(cl_ins_fmul, fmul);
binary_ins!(cl_ins_fdiv, fdiv);

macro_rules! unary_ins {
    ($name:ident, $method:ident) => {
        #[no_mangle]
        pub unsafe extern "C" fn $name(builder: *mut FunctionBuilder<'static>, value: u32) -> u32 {
            (*builder).ins().$method(Value::from_u32(value)).as_u32()
        }
    };
}
unary_ins!(cl_ins_ineg, ineg);
unary_ins!(cl_ins_bnot, bnot);
unary_ins!(cl_ins_clz, clz);
unary_ins!(cl_ins_ctz, ctz);
unary_ins!(cl_ins_popcnt, popcnt);
unary_ins!(cl_ins_bswap, bswap);

#[no_mangle]
pub unsafe extern "C" fn cl_ins_f32const(builder: *mut FunctionBuilder<'static>, bits: u32) -> u32 {
    (*builder).ins().f32const(cranelift_codegen::ir::immediates::Ieee32::with_bits(bits)).as_u32()
}

#[no_mangle]
pub unsafe extern "C" fn cl_ins_f64const(builder: *mut FunctionBuilder<'static>, bits: u64) -> u32 {
    (*builder).ins().f64const(cranelift_codegen::ir::immediates::Ieee64::with_bits(bits)).as_u32()
}

#[no_mangle]
pub unsafe extern "C" fn cl_ins_symbol_value(builder: *mut FunctionBuilder<'static>, value_type: u16, global: u32) -> u32 {
    (*builder).ins().symbol_value(ty(value_type), GlobalValue::from_u32(global)).as_u32()
}

macro_rules! typed_unary_ins {
    ($name:ident, $method:ident) => {
        #[no_mangle]
        pub unsafe extern "C" fn $name(builder: *mut FunctionBuilder<'static>, value_type: u16, value: u32) -> u32 {
            (*builder).ins().$method(ty(value_type), Value::from_u32(value)).as_u32()
        }
    };
}
typed_unary_ins!(cl_ins_ireduce, ireduce);
typed_unary_ins!(cl_ins_uextend, uextend);
typed_unary_ins!(cl_ins_sextend, sextend);

// reinterpret bits using the function local flags just as load and store do
#[no_mangle]
pub unsafe extern "C" fn cl_ins_bitcast(builder: *mut FunctionBuilder<'static>, value_type: u16, flags: u32, value: u32) -> u32 {
    let Some(flags) = MemFlags::with_number(flags) else { error("invalid MemFlags"); return INVALID };
    let builder = &mut *builder;
    if !builder.func.dfg.mem_flags.is_valid(flags) { error("unknown MemFlags"); return INVALID }
    let flags = builder.func.dfg.mem_flags[flags];
    builder.ins().bitcast(ty(value_type), flags, Value::from_u32(value)).as_u32()
}

// copy the sign bit of right onto left without changing the other bits
binary_ins!(cl_ins_fcopysign, fcopysign);
// choose the smaller float and propagate nan with negative zero preferred over positive zero
binary_ins!(cl_ins_fmin, fmin);
// choose the larger float and propagate nan with positive zero preferred over negative zero
binary_ins!(cl_ins_fmax, fmax);
// compute the floating point square root
unary_ins!(cl_ins_sqrt, sqrt);
// flip the floating point sign bit without changing the other bits
unary_ins!(cl_ins_fneg, fneg);
// clear the floating point sign bit without changing the other bits
unary_ins!(cl_ins_fabs, fabs);
// round toward positive infinity and keep the floating point type
unary_ins!(cl_ins_ceil, ceil);
// round toward negative infinity and keep the floating point type
unary_ins!(cl_ins_floor, floor);
// round toward zero and keep the floating point type
unary_ins!(cl_ins_trunc, trunc);
// round to the nearest integral float with ties to even
unary_ins!(cl_ins_nearest, nearest);
// convert f64x2 to f32x4 with rounding to nearest ties to even and zero the upper two lanes
unary_ins!(cl_ins_fvdemote, fvdemote);
// convert the lower two lanes of f32x4 to f64x2 and discard the upper lanes
unary_ins!(cl_ins_fvpromote_low, fvpromote_low);
// convert a scalar float to a wider float type preserving its numerical value
typed_unary_ins!(cl_ins_fpromote, fpromote);
// convert a scalar float to a narrower float type with rounding to nearest ties to even
typed_unary_ins!(cl_ins_fdemote, fdemote);
// convert a scalar float to an unsigned integer toward zero and trap on nan or overflow
typed_unary_ins!(cl_ins_fcvt_to_uint, fcvt_to_uint);
// convert a scalar float to a signed integer toward zero and trap on nan or overflow
typed_unary_ins!(cl_ins_fcvt_to_sint, fcvt_to_sint);
// convert float lanes to unsigned integers toward zero with clamping and nan converted to zero
typed_unary_ins!(cl_ins_fcvt_to_uint_sat, fcvt_to_uint_sat);
// convert float lanes to signed integers toward zero with clamping and nan converted to zero
typed_unary_ins!(cl_ins_fcvt_to_sint_sat, fcvt_to_sint_sat);
// convert unsigned integer lanes to floats with rounding to nearest ties to even
typed_unary_ins!(cl_ins_fcvt_from_uint, fcvt_from_uint);
// convert signed integer lanes to floats with rounding to nearest ties to even
typed_unary_ins!(cl_ins_fcvt_from_sint, fcvt_from_sint);

// compute a fused multiply add with a single rounding
#[no_mangle]
pub unsafe extern "C" fn cl_ins_fma(builder: *mut FunctionBuilder<'static>, left: u32, right: u32, addend: u32) -> u32 {
    (*builder).ins().fma(Value::from_u32(left), Value::from_u32(right), Value::from_u32(addend)).as_u32()
}

// decode every float condition explicitly without relying on the rust enum layout
#[no_mangle]
pub unsafe extern "C" fn cl_ins_fcmp(builder: *mut FunctionBuilder<'static>, condition: u32, left: u32, right: u32) -> u32 {
    let condition = match condition {
        0 => FloatCC::Ordered,
        1 => FloatCC::Unordered,
        2 => FloatCC::Equal,
        3 => FloatCC::NotEqual,
        4 => FloatCC::OrderedNotEqual,
        5 => FloatCC::UnorderedOrEqual,
        6 => FloatCC::LessThan,
        7 => FloatCC::LessThanOrEqual,
        8 => FloatCC::GreaterThan,
        9 => FloatCC::GreaterThanOrEqual,
        10 => FloatCC::UnorderedOrLessThan,
        11 => FloatCC::UnorderedOrLessThanOrEqual,
        12 => FloatCC::UnorderedOrGreaterThan,
        13 => FloatCC::UnorderedOrGreaterThanOrEqual,
        _ => { error("invalid FloatCC"); return INVALID }
    };
    (*builder).ins().fcmp(condition, Value::from_u32(left), Value::from_u32(right)).as_u32()
}

// expose both immediate extension modes without changing cranelift instruction semantics
macro_rules! immediate_ins {
    ($signed:ident, $unsigned:ident, $signed_method:ident, $unsigned_method:ident) => {
        #[no_mangle]
        pub unsafe extern "C" fn $signed(builder: *mut FunctionBuilder<'static>, value: u32, immediate: i64) -> u32 {
            (*builder).ins().$signed_method(Value::from_u32(value), immediate).as_u32()
        }
        #[no_mangle]
        pub unsafe extern "C" fn $unsigned(builder: *mut FunctionBuilder<'static>, value: u32, immediate: u64) -> u32 {
            (*builder).ins().$unsigned_method(Value::from_u32(value), immediate as i64).as_u32()
        }
    };
}
immediate_ins!(cl_ins_iadd_imm_s, cl_ins_iadd_imm_u, iadd_imm_s, iadd_imm_u);
immediate_ins!(cl_ins_imul_imm_s, cl_ins_imul_imm_u, imul_imm_s, imul_imm_u);
immediate_ins!(cl_ins_udiv_imm_s, cl_ins_udiv_imm_u, udiv_imm_s, udiv_imm_u);
immediate_ins!(cl_ins_sdiv_imm_s, cl_ins_sdiv_imm_u, sdiv_imm_s, sdiv_imm_u);
immediate_ins!(cl_ins_urem_imm_s, cl_ins_urem_imm_u, urem_imm_s, urem_imm_u);
immediate_ins!(cl_ins_srem_imm_s, cl_ins_srem_imm_u, srem_imm_s, srem_imm_u);
immediate_ins!(cl_ins_band_imm_s, cl_ins_band_imm_u, band_imm_s, band_imm_u);
immediate_ins!(cl_ins_bor_imm_s, cl_ins_bor_imm_u, bor_imm_s, bor_imm_u);
immediate_ins!(cl_ins_bxor_imm_s, cl_ins_bxor_imm_u, bxor_imm_s, bxor_imm_u);
immediate_ins!(cl_ins_rotl_imm_s, cl_ins_rotl_imm_u, rotl_imm_s, rotl_imm_u);
immediate_ins!(cl_ins_rotr_imm_s, cl_ins_rotr_imm_u, rotr_imm_s, rotr_imm_u);
immediate_ins!(cl_ins_ishl_imm_s, cl_ins_ishl_imm_u, ishl_imm_s, ishl_imm_u);
immediate_ins!(cl_ins_ushr_imm_s, cl_ins_ushr_imm_u, ushr_imm_s, ushr_imm_u);
immediate_ins!(cl_ins_sshr_imm_s, cl_ins_sshr_imm_u, sshr_imm_s, sshr_imm_u);

#[no_mangle]
pub unsafe extern "C" fn cl_ins_iadd_imm(builder: *mut FunctionBuilder<'static>, value: u32, immediate: i64) -> u32 {
    (*builder).ins().iadd_imm_s(Value::from_u32(value), immediate).as_u32()
}

fn intcc(value: u32) -> Option<IntCC> {
    use IntCC::*;
    match value {
        0 => Some(Equal), 1 => Some(NotEqual), 2 => Some(SignedLessThan),
        3 => Some(SignedGreaterThanOrEqual), 4 => Some(SignedGreaterThan),
        5 => Some(SignedLessThanOrEqual), 6 => Some(UnsignedLessThan),
        7 => Some(UnsignedGreaterThanOrEqual), 8 => Some(UnsignedGreaterThan),
        9 => Some(UnsignedLessThanOrEqual), _ => { error("invalid IntCC"); None }
    }
}

#[no_mangle]
pub unsafe extern "C" fn cl_ins_icmp(builder: *mut FunctionBuilder<'static>, condition: u32, left: u32, right: u32) -> u32 {
    let Some(condition) = intcc(condition) else { return INVALID };
    (*builder).ins().icmp(condition, Value::from_u32(left), Value::from_u32(right)).as_u32()
}


// compare using a sign-extended immediate and the supplied integer condition
#[no_mangle]
pub unsafe extern "C" fn cl_ins_icmp_imm_s(builder: *mut FunctionBuilder<'static>, condition: u32, value: u32, immediate: i64) -> u32 {
    let Some(condition) = intcc(condition) else { return INVALID };
    (*builder).ins().icmp_imm_s(condition, Value::from_u32(value), immediate).as_u32()
}

// compare using a zero-extended immediate while preserving its full unsigned bit pattern
#[no_mangle]
pub unsafe extern "C" fn cl_ins_icmp_imm_u(builder: *mut FunctionBuilder<'static>, condition: u32, value: u32, immediate: u64) -> u32 {
    let Some(condition) = intcc(condition) else { return INVALID };
    (*builder).ins().icmp_imm_u(condition, Value::from_u32(value), immediate as i64).as_u32()
}

// preserve the historical signed immediate comparison helper
#[no_mangle]
pub unsafe extern "C" fn cl_ins_icmp_imm(builder: *mut FunctionBuilder<'static>, condition: u32, value: u32, immediate: i64) -> u32 {
    cl_ins_icmp_imm_s(builder, condition, value, immediate)
}

#[no_mangle]
pub unsafe extern "C" fn cl_ins_select(builder: *mut FunctionBuilder<'static>, condition: u32, if_true: u32, if_false: u32) -> u32 {
    (*builder).ins().select(Value::from_u32(condition), Value::from_u32(if_true), Value::from_u32(if_false)).as_u32()
}

// select each bit from if_true where the mask bit is set and from if_false otherwise
#[no_mangle]
pub unsafe extern "C" fn cl_ins_bitselect(builder: *mut FunctionBuilder<'static>, mask: u32, if_true: u32, if_false: u32) -> u32 {
    (*builder).ins().bitselect(Value::from_u32(mask), Value::from_u32(if_true), Value::from_u32(if_false)).as_u32()
}

#[no_mangle]
pub unsafe extern "C" fn cl_ins_jump(builder: *mut FunctionBuilder<'static>, destination: u32, args: *const u32, len: usize) -> u32 {
    let Some(args) = values(args, len) else { return INVALID };
    let args: Vec<BlockArg> = args.into_iter().map(Into::into).collect();
    (*builder).ins().jump(Block::from_u32(destination), &args).as_u32()
}

// terminate the current block with a native trap carrying the supplied reason code
#[no_mangle]
pub unsafe extern "C" fn cl_ins_trap(builder: *mut FunctionBuilder<'static>, code: u8) -> u32 {
    let Some(code) = trapcode(code) else { return INVALID };
    (*builder).ins().trap(code).as_u32()
}

// emit a native trap when the scalar integer condition is zero
#[no_mangle]
pub unsafe extern "C" fn cl_ins_trapz(builder: *mut FunctionBuilder<'static>, condition: u32, code: u8) -> u32 {
    let Some(code) = trapcode(code) else { return INVALID };
    (*builder).ins().trapz(Value::from_u32(condition), code).as_u32()
}

// emit a native trap when the scalar integer condition is nonzero
#[no_mangle]
pub unsafe extern "C" fn cl_ins_trapnz(builder: *mut FunctionBuilder<'static>, condition: u32, code: u8) -> u32 {
    let Some(code) = trapcode(code) else { return INVALID };
    (*builder).ins().trapnz(Value::from_u32(condition), code).as_u32()
}

// emit a native debugger breakpoint without terminating the block
#[no_mangle]
pub unsafe extern "C" fn cl_ins_debugtrap(builder: *mut FunctionBuilder<'static>) -> u32 {
    (*builder).ins().debugtrap().as_u32()
}

// add unsigned scalar integers and trap with the supplied reason if the sum overflows
#[no_mangle]
pub unsafe extern "C" fn cl_ins_uadd_overflow_trap(builder: *mut FunctionBuilder<'static>, left: u32, right: u32, code: u8) -> u32 {
    let Some(code) = trapcode(code) else { return INVALID };
    (*builder).ins().uadd_overflow_trap(Value::from_u32(left), Value::from_u32(right), code).as_u32()
}

#[no_mangle]
pub unsafe extern "C" fn cl_ins_brif(builder: *mut FunctionBuilder<'static>, condition: u32, then_block: u32, then_args: *const u32, then_len: usize, else_block: u32, else_args: *const u32, else_len: usize) -> u32 {
    let (Some(then_args), Some(else_args)) = (values(then_args, then_len), values(else_args, else_len)) else { return INVALID };
    let then_args: Vec<BlockArg> = then_args.into_iter().map(Into::into).collect();
    let else_args: Vec<BlockArg> = else_args.into_iter().map(Into::into).collect();
    (*builder).ins().brif(Value::from_u32(condition), Block::from_u32(then_block), &then_args, Block::from_u32(else_block), &else_args).as_u32()
}

#[no_mangle]
pub unsafe extern "C" fn cl_ins_nop(builder: *mut FunctionBuilder<'static>) -> u32 {
    (*builder).ins().nop().as_u32()
}

#[no_mangle]
pub unsafe extern "C" fn cl_ins_return(builder: *mut FunctionBuilder<'static>, args: *const u32, len: usize) -> u32 {
    let Some(args) = values(args, len) else { return INVALID };
    (*builder).ins().return_(&args).as_u32()
}

#[no_mangle]
pub unsafe extern "C" fn cl_ins_call(builder: *mut FunctionBuilder<'static>, function: u32, args: *const u32, len: usize) -> u32 {
    let Some(args) = values(args, len) else { return INVALID };
    (*builder).ins().call(FuncRef::from_u32(function), &args).as_u32()
}

#[no_mangle]
pub unsafe extern "C" fn cl_ins_call_indirect(builder: *mut FunctionBuilder<'static>, signature: u32, callee: u32, args: *const u32, len: usize) -> u32 {
    let Some(args) = values(args, len) else { return INVALID };
    (*builder).ins().call_indirect(SigRef::from_u32(signature), Value::from_u32(callee), &args).as_u32()
}

#[no_mangle]
pub unsafe extern "C" fn cl_memflags_new(builder: *mut FunctionBuilder<'static>, trusted: bool) -> u32 {
    let flags = if trusted { MemFlagsData::trusted() } else { MemFlagsData::new() };
    let builder = &mut *builder;
    builder.func.dfg.mem_flags.insert_unchecked(flags).index() as u32
}

#[no_mangle]
pub unsafe extern "C" fn cl_memflags_with_endianness(builder: *mut FunctionBuilder<'static>, flags: u32, endianness: u32) -> u32 {
    let Some(flags) = MemFlags::with_number(flags) else { error("invalid MemFlags"); return INVALID };
    let endianness = match endianness {
        0 => Endianness::Little, 1 => Endianness::Big,
        _ => { error("invalid Endianness"); return INVALID }
    };
    let builder = &mut *builder;
    if !builder.func.dfg.mem_flags.is_valid(flags) { error("unknown MemFlags"); return INVALID }
    let flags = builder.func.dfg.mem_flags[flags].with_endianness(endianness);
    builder.func.dfg.mem_flags.insert_unchecked(flags).index() as u32
}

#[no_mangle]
pub unsafe extern "C" fn cl_ins_load(builder: *mut FunctionBuilder<'static>, value_type: u16, flags: u32, address: u32, offset: i32) -> u32 {
    let Some(flags) = MemFlags::with_number(flags) else { error("invalid MemFlags"); return INVALID };
    let builder = &mut *builder;
    let flags = builder.func.dfg.mem_flags[flags];
    builder.ins().load(ty(value_type), flags, Value::from_u32(address), offset).as_u32()
}

#[no_mangle]
pub unsafe extern "C" fn cl_ins_store(builder: *mut FunctionBuilder<'static>, flags: u32, value: u32, address: u32, offset: i32) -> u32 {
    let Some(flags) = MemFlags::with_number(flags) else { error("invalid MemFlags"); return INVALID };
    let builder = &mut *builder;
    let flags = builder.func.dfg.mem_flags[flags];
    builder.ins().store(flags, Value::from_u32(value), Value::from_u32(address), offset).as_u32()
}
