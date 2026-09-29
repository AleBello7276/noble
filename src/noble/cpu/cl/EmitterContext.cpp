#include "CraneliftJIT.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace {

static_assert(sizeof(GPR) == sizeof(std::uint64_t));
static_assert(sizeof(FPR) == sizeof(double));
static_assert(offsetof(PPCContext, SPRs) + sizeof(SPRState) <= (std::numeric_limits<std::int32_t>::max)());

constexpr std::int32_t GPROffset(size_t index) {
    return static_cast<std::int32_t>(offsetof(PPCContext, GPRs) + index * sizeof(GPR));
}

constexpr std::int32_t FPROffset(size_t index) {
    return static_cast<std::int32_t>(offsetof(PPCContext, FPRs) + index * sizeof(FPR));
}

constexpr std::int32_t SPROffset(size_t struct_offset) {
    return static_cast<std::int32_t>(offsetof(PPCContext, SPRs) + struct_offset);
}

void CheckSPROffset(size_t offset) {
    if (offset % sizeof(std::uint64_t) != 0 || offset > sizeof(SPRState) - sizeof(std::uint64_t))
        throw std::out_of_range("invalid 64 bit spr register offset");
}

}  // namespace

cranelift::Value EmitterContext::LoadGPR(size_t index) {
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

cranelift::Value EmitterContext::StoreGPR(size_t index, cranelift::Value value) {
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

cranelift::Value EmitterContext::LoadFPR(size_t index) {
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

cranelift::Value EmitterContext::StoreFPR(size_t index, cranelift::Value value) {
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

cranelift::Value EmitterContext::LoadSPR(size_t struct_offset) {
    CheckSPROffset(struct_offset);

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

cranelift::Value EmitterContext::StoreSPR(size_t struct_offset, cranelift::Value value) {
    CheckSPROffset(struct_offset);

#if NOBLE_CRANELIFT_REGISTER_CACHE

    sprCache_[struct_offset] = {value, true};
#else

    builder.ins().store(builder.memflags_new(), value, vCpuState, SPROffset(struct_offset));

#endif

    return value;
}

cranelift::Value EmitterContext::LoadSPR(eSPR type) {
    switch (type) {
    case eSPR::LR:
        return LoadSPR(offsetof(SPRState, LR));
    default:
        throw std::invalid_argument("unsupported spr number");
    }
}

cranelift::Value EmitterContext::StoreSPR(eSPR type, cranelift::Value value) {
    switch (type) {
    case eSPR::LR:
        return StoreSPR(offsetof(SPRState, LR), value);
    default:
        throw std::invalid_argument("unsupported spr number");
    }
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
}

cranelift::Inst EmitterContext::Return() {
    FlushState();
    return builder.ins().return_();
}

cranelift::Inst EmitterContext::Jump(cranelift::Block destination, std::span<const cranelift::Value> args) {
    FlushState();
    return builder.ins().jump(destination, args);
}

cranelift::Inst EmitterContext::Branch(cranelift::Value condition, cranelift::Block then_block,
                                       std::span<const cranelift::Value> then_args,
                                       cranelift::Block else_block,
                                       std::span<const cranelift::Value> else_args) {
    FlushState();
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
