#include "EmitterContext.h"
#include "emit/cl_util.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

cranelift::Value EmitterContext::load_gpr(size_t index) {
    if (index >= PPCContext::GPR_COUNT)
        throw std::out_of_range("gpr index");

#if NOBLE_CRANELIFT_REGISTER_CACHE

    // try to get cached Value
    auto& cached = gprCache_[index];
    if (cached.value != cranelift::INVALID_ID)
        return cached.value;
#endif

    // perform i64 load from PPCContext
    const auto value
        = builder.ins().load(cranelift::types::I64(), builder.memflags_new(), vCpuState, GPROffset(index));

#if NOBLE_CRANELIFT_REGISTER_CACHE
    // if Value is not inside the current block cache, add it
    cached.value = value;
#endif

    return value;
}

cranelift::Value EmitterContext::store_gpr(size_t index, cranelift::Value value) {
    if (index >= PPCContext::GPR_COUNT)
        throw std::out_of_range("gpr index");

#if NOBLE_CRANELIFT_REGISTER_CACHE
    // update the Value in the cache with the new one, and mark it as dirty
    gprCache_[index] = {value, true};
#else
    // perform store in PPCContext
    builder.ins().store(builder.memflags_new(), value, vCpuState, GPROffset(index));
#endif

    return value;
}

cranelift::Value EmitterContext::load_fpr(size_t index) {
    if (index >= PPCContext::FPR_COUNT)
        throw std::out_of_range("fpr index");

#if NOBLE_CRANELIFT_REGISTER_CACHE

    // try to get cached Value
    auto& cached = fprCache_[index];
    if (cached.value != cranelift::INVALID_ID)
        return cached.value;
#endif

    // perform f64 load from PPCContext
    const auto value
        = builder.ins().load(cranelift::types::F64(), builder.memflags_new(), vCpuState, FPROffset(index));

#if NOBLE_CRANELIFT_REGISTER_CACHE
    // if Value is not inside the current block cache, add it
    cached.value = value;
#endif
    return value;
}

cranelift::Value EmitterContext::store_fpr(size_t index, cranelift::Value value) {
    if (index >= PPCContext::FPR_COUNT)
        throw std::out_of_range("fpr index");

#if NOBLE_CRANELIFT_REGISTER_CACHE
    // update the Value in the cache with the new one, and mark it as dirty
    fprCache_[index] = {value, true};
#else
    // perform store in PPCContext
    builder.ins().store(builder.memflags_new(), value, vCpuState, FPROffset(index));
#endif

    return value;
}

cranelift::Value EmitterContext::load_spr(size_t struct_offset) {
#if NOBLE_CRANELIFT_REGISTER_CACHE

    auto& cached = sprCache_[struct_offset];
    if (cached.value != cranelift::INVALID_ID)
        return cached.value;
#endif

    const auto value = builder.ins().load(cranelift::types::I64(), builder.memflags_new(), vCpuState,
                                          SPROffset(struct_offset));

#if NOBLE_CRANELIFT_REGISTER_CACHE
    cached.value = value;
#endif
    return value;
}

cranelift::Value EmitterContext::store_spr(size_t struct_offset, cranelift::Value value) {
#if NOBLE_CRANELIFT_REGISTER_CACHE

    sprCache_[struct_offset] = {value, true};
#else

    builder.ins().store(builder.memflags_new(), value, vCpuState, SPROffset(struct_offset));

#endif

    return value;
}

cranelift::Value EmitterContext::load_spr(eSPR type) {
    switch (type) {
    case eSPR::XER: {
        constexpr size_t offset = offsetof(SPRState, XER);
#if NOBLE_CRANELIFT_REGISTER_CACHE
        auto& cached = sprCache_[offset];
        if (cached.value != cranelift::INVALID_ID)
            return zext(cranelift::types::I64(), cached.value);
#endif
        const auto value
            = ins().load(cranelift::types::I32(), builder.memflags_new(), vCpuState, SPROffset(offset));
#if NOBLE_CRANELIFT_REGISTER_CACHE
        cached.value = value;
#endif
        return zext64(value);
    }
    case eSPR::LR:
        return load_spr(offsetof(SPRState, LR));
    case eSPR::CTR:
        return load_spr(offsetof(SPRState, CTR));
    default:
        throw std::invalid_argument("unsupported spr number");
    }
}

cranelift::Value EmitterContext::store_spr(eSPR type, cranelift::Value value) {
    switch (type) {
    case eSPR::XER:
        return store_spr(offsetof(SPRState, XER), builder.ins().ireduce(cranelift::types::I32(), value));
    case eSPR::LR:
        return store_spr(offsetof(SPRState, LR), value);
    case eSPR::CTR:
        return store_spr(offsetof(SPRState, CTR), value);
    default:
        throw std::invalid_argument("unsupported spr number");
    }
}

void EmitterContext::store_ca(cranelift::Value value) {
    ins().store(builder.memflags_new(), value, vCpuState, GetXER_CA_Offset());
}

cranelift::Value EmitterContext::load_ca() {
    return builder.ins().load(cranelift::types::I8(), builder.memflags_new(), vCpuState, GetXER_CA_Offset());
}

void EmitterContext::FlushState() {
#if NOBLE_CRANELIFT_REGISTER_CACHE
    for (size_t index = 0; index < gprCache_.size(); ++index) {
        auto& cached = gprCache_[index];

        if (cached.dirty) {
            builder.ins().store(builder.memflags_new(), cached.value, vCpuState, GPROffset(index));
            cached.dirty = false;
        }
    }

    for (size_t index = 0; index < fprCache_.size(); ++index) {
        auto& cached = fprCache_[index];

        if (cached.dirty) {
            builder.ins().store(builder.memflags_new(), cached.value, vCpuState, FPROffset(index));
            cached.dirty = false;
        }
    }

    for (auto& [field, cached] : sprCache_) {
        if (cached.dirty) {
            builder.ins().store(builder.memflags_new(), cached.value, vCpuState, SPROffset(field));
            cached.dirty = false;
        }
    }
#endif
}

void EmitterContext::InvalidateState() {
#if NOBLE_CRANELIFT_REGISTER_CACHE
    for (const auto& cached : gprCache_)
        if (cached.dirty)
            throw std::logic_error("dirty gpr cache must be flushed before invalidation");

    for (const auto& cached : fprCache_)
        if (cached.dirty)
            throw std::logic_error("dirty fpr cache must be flushed before invalidation");

    for (const auto& [field, cached] : sprCache_)
        if (cached.dirty)
            throw std::logic_error("dirty spr cache must be flushed before invalidation");

    gprCache_.fill({});
    fprCache_.fill({});
    sprCache_.clear();
#endif
}

void EmitterContext::SwitchToBlock(cranelift::Block block) {
    InvalidateState();
    builder.switch_to_block(block);
    terminated = false;
}

cranelift::Inst EmitterContext::Return() {
    FlushState();
    terminated = true;
    return builder.ins().return_();
}

cranelift::Inst EmitterContext::Jump(cranelift::Block destination, std::span<const cranelift::Value> args) {
    FlushState();
    terminated = true;
    return builder.ins().jump(destination, args);
}

cranelift::Inst EmitterContext::Branch(cranelift::Value condition, cranelift::Block then_block,
                                       std::span<const cranelift::Value> then_args,
                                       cranelift::Block else_block,
                                       std::span<const cranelift::Value> else_args) {
    FlushState();

    terminated = true;
    return builder.ins().brif(condition, then_block, then_args, else_block, else_args);
}

cranelift::Inst EmitterContext::Call(cranelift::FuncRef function, std::span<const cranelift::Value> args) {
    FlushState();
    const auto inst = builder.ins().call(function, args);
    InvalidateState();
    return inst;
}

cranelift::Inst EmitterContext::CallGuest(cranelift::FuncRef function) {
    const std::array args{vCpuState, vMemBase};
    return Call(function, args);
}

cranelift::Inst EmitterContext::CallIndirect(cranelift::SigRef signature, cranelift::Value callee,
                                             std::span<const cranelift::Value> args) {
    FlushState();
    const auto inst = builder.ins().call_indirect(signature, callee, args);
    InvalidateState();
    return inst;
}

cranelift::Value EmitterContext::load_memory(cranelift::Value ea, cranelift::Type load_type) {
    const cranelift::Value mem = vMemBase;

    ea = zext64(ins().ireduce(cranelift::types::I32(), ea));
    const auto addr = builder.ins().iadd(mem, ea);

    return builder.ins().load(load_type, builder.memflags_new(), addr, 0);
}

cranelift::Value EmitterContext::load_memory(cranelift::Value base, cranelift::Value offset,
                                             cranelift::Type load_type) {
    const auto ea = builder.ins().iadd(base, offset);

    return load_memory(ea, load_type);
}

void EmitterContext::store_memory(cranelift::Value ea, cranelift::Value value) {
    const cranelift::Value mem = vMemBase;

    ea = zext64(ins().ireduce(cranelift::types::I32(), ea));
    const auto addr = builder.ins().iadd(mem, ea);

    builder.ins().store(builder.memflags_new(), value, addr, 0);
}

void EmitterContext::store_memory(cranelift::Value base, cranelift::Value offset, cranelift::Value value) {
    const auto ea = builder.ins().iadd(base, offset);

    store_memory(ea, value);
}

template <bool Signed>
void EmitterContext::record_cr(size_t field, cranelift::Value lhs, cranelift::Value rhs) {
    using namespace cranelift;

    constexpr unsigned LT_BIT_MASK = 1 << 0;
    constexpr unsigned GT_BIT_MASK = 1 << 8;
    constexpr unsigned EQ_BIT_MASK = 1 << 16;
    Value lt;
    Value gt;

    if constexpr (Signed) {
        lt = ins().icmp(IntCC::CL_INTCC_SIGNED_LESS_THAN, lhs, rhs);
        gt = ins().icmp(IntCC::CL_INTCC_SIGNED_GREATER_THAN, lhs, rhs);
    } else {
        lt = ins().icmp(IntCC::CL_INTCC_UNSIGNED_LESS_THAN, lhs, rhs);
        gt = ins().icmp(IntCC::CL_INTCC_UNSIGNED_GREATER_THAN, lhs, rhs);
    }

    // pack the comparisons into a i32 Value, only one bit of the three can be 1 at a time, the EQ is neither
    // LT or GT
    const Value packed
        = ins().select(lt, i32(LT_BIT_MASK), ins().select(gt, i32(GT_BIT_MASK), i32(EQ_BIT_MASK)));

    // TODO: handle SO bit, under a config flag

    ins().store(builder.memflags_new(), packed, vCpuState, CRFieldBitOffset(field));
}

template <bool Signed>
void EmitterContext::record_cr(size_t field, cranelift::Value lhs) {
    record_cr<Signed>(field, ins().ireduce(cranelift::types::I32(), lhs), i32(0));
}

template void EmitterContext::record_cr<true>(size_t, cranelift::Value, cranelift::Value);
template void EmitterContext::record_cr<false>(size_t, cranelift::Value, cranelift::Value);
template void EmitterContext::record_cr<true>(size_t, cranelift::Value);
template void EmitterContext::record_cr<false>(size_t, cranelift::Value);

cranelift::Value EmitterContext::get_cr_field(size_t field, size_t bit) {
    const auto value = builder.ins().load(cranelift::types::I8(), builder.memflags_new(), vCpuState,
                                          CRFieldBitOffset(field, bit));
    return value;
}

// this is a copy of what xenia is doing
void EmitterContext::update_fpscr(bool rc) {
    // FX  - preserved
    // FEX - cleared
    // VX  - cleared
    // OX  - preserved

    Value fpscr = load_fpscr();

    fpscr = ins().band_imm_u(fpscr, 0x9FFFFFFFu);

    store_fpscr(fpscr);

    if (rc) {
        // Xenia currently computes FX/FEX/VX/OX as zero,
        // so arithmetic record forms produce CR1 = 0000.
        ins().store(builder.memflags_new(), i32(0), vCpuState, CRFieldBitOffset(1));
    }
}

cranelift::Value EmitterContext::load_fpscr() {
    return ins().load(types::I32(), builder.memflags_new(), vCpuState,
                      static_cast<int32_t>(offsetof(PPCContext, FPSCR)));
}

void EmitterContext::store_fpscr(Value value) {
    ins().store(builder.memflags_new(), value, vCpuState, static_cast<int32_t>(offsetof(PPCContext, FPSCR)));
}
