#pragma once

#include "cl_util.h"

static inline void branch_fallback(EmitterContext& e_, Value nia) {
    e_.ins().store(e_.builder.memflags_new(), nia, e_.vCpuState, offsetof(PPCContext, NIA));
    e_.Return();
}

static inline void branch_call(EmitterContext& e_, InstructionInfo& info_, FuncId function, bool LK) {
    const GuestAddress nextAddress = info_.mAddress + 4;

    if (LK)
        e_.ins().store(e_.builder.memflags_new(), e_.i32(nextAddress), e_.vCpuState,
                       offsetof(PPCContext, NIA));

    e_.CallGuest(e_.builder.declare_func_in_func(e_.jit, function));
    if (!LK) {
        e_.Return();
        return;
    }

    const Value nia
        = e_.ins().load(types::I32(), e_.builder.memflags_new(), e_.vCpuState, offsetof(PPCContext, NIA));

    const Value action
        = e_.ins().load(types::I32(), e_.builder.memflags_new(), e_.vCpuState, offsetof(PPCContext, Action));

    const Value resumed = e_.ins().band(
        e_.ins().icmp(IntCC::CL_INTCC_EQUAL, nia, e_.i32(nextAddress)),
        e_.ins().icmp(IntCC::CL_INTCC_EQUAL, action, e_.i32(static_cast<uint32_t>(HostAction::None))));

    const Block continuation = e_.builder.create_block();
    const Block dispatched = e_.builder.create_block();

    e_.Branch(resumed, continuation, {}, dispatched, {});
    e_.SwitchToBlock(dispatched);
    e_.Return();

    e_.SwitchToBlock(continuation);
}

static inline void branch(EmitterContext& e_, InstructionInfo& info_, GuestAddress NIA, bool LK) {
    if (!LK || NIA == info_.mAddress + 4) {
        const Block label = e_.BlockLookup(NIA);
        if (label != INVALID_ID) {
            e_.Jump(label);
            return;
        }
    }

    if (const auto function = e_.backend->FindFunction(NIA)) {
        branch_call(e_, info_, function->m_id, LK);
        return;
    }

    branch_fallback(e_, e_.i32(NIA));
}

static inline void branch_indirect(EmitterContext& e_, InstructionInfo& info_, Value NIA, bool LK) {
    const auto blocks = e_.clBlockMap;

    if (!LK) {
        for (const auto& [address, label] : blocks) {
            const Block next = e_.builder.create_block();
            const Value matches = e_.ins().icmp(IntCC::CL_INTCC_EQUAL, NIA, e_.i32(address));

            e_.Branch(matches, label, {}, next, {});
            e_.SwitchToBlock(next);
        }
    }

    const auto functions = e_.backend ? e_.backend->CallableFunctions() : std::vector<JITFunction>{};
    const Block continuation = LK && !functions.empty() ? e_.builder.create_block() : INVALID_ID;

    for (const auto& function : functions) {
        const Block call = e_.builder.create_block();
        const Block next = e_.builder.create_block();
        const Value matches = e_.ins().icmp(IntCC::CL_INTCC_EQUAL, NIA, e_.i32(function.mStartAddress));

        e_.Branch(matches, call, {}, next, {});
        e_.SwitchToBlock(call);
        branch_call(e_, info_, function.m_id, LK);

        if (!e_.terminated)
            e_.Jump(continuation);

        e_.SwitchToBlock(next);
    }

    branch_fallback(e_, NIA);

    if (continuation != INVALID_ID)
        e_.SwitchToBlock(continuation);
}

CLHandler(b) {
    const auto lk = info_.mInst.field_lk();
    const auto aa = info_.mInst.field_aa();
    const auto li = info_.mInst.field_li();

    assert(!aa);  // NOT YET IMPLEMENTED

    GuestAddress NIA = info_.mAddress + sign_extend<26>(li);

    if (lk) {
        Value return_address = e_.i64(info_.mAddress + 4);
        e_.store_spr(eSPR::LR, return_address);
    }

    branch(e_, info_, NIA, lk);
}

CLHandler(bc) {
    const auto bo = info_.mInst.field_bo();
    const auto bi = info_.mInst.field_bi();
    const auto lk = info_.mInst.field_lk();

    Value ctr_ok = e_.i8(1);
    if ((bo & 4) == 0) {
        const Value ctr = e_.ins().iadd_imm(e_.load_spr(eSPR::CTR), -1);
        e_.store_spr(eSPR::CTR, ctr);
        ctr_ok = e_.ins().icmp((bo & 2) ? IntCC::CL_INTCC_EQUAL : IntCC::CL_INTCC_NOT_EQUAL, ctr, e_.i64(0));
    }

    Value cond_ok = e_.i8(1);
    if ((bo & 16) == 0) {
        const Value bit = e_.get_cr_field(bi >> 2, bi & 3);
        cond_ok = e_.ins().icmp(IntCC::CL_INTCC_EQUAL, bit, e_.i8((bo & 8) != 0));
    }

    // lk updates lr regardless of whether the conditional branch is taken
    if (lk)
        e_.store_spr(eSPR::LR, e_.i64(info_.mAddress + 4));

    const Block b_True = e_.builder.create_block();
    const Block b_False = e_.builder.create_block();
    const Value do_branch = e_.ins().band(ctr_ok, cond_ok);
    e_.Branch(do_branch, b_True, {}, b_False, {});
    e_.SwitchToBlock(b_True);

    const GuestAddress NIA = info_.mInst.branch_dest(info_.mAddress).value();
    branch(e_, info_, NIA, lk);
    if (!e_.terminated)
        e_.Jump(b_False);

    e_.SwitchToBlock(b_False);
}

CLHandler(bclr) {
    const auto bo = info_.mInst.field_bo();
    const auto bi = info_.mInst.field_bi();
    const bool lk = info_.mInst.field_lk();

    // capture the old lr before lk replaces it with the return address
    const Value target = e_.ins().band(e_.ins().ireduce(types::I32(), e_.load_spr(eSPR::LR)), e_.i32(-4));

    Value ctr_ok = e_.i8(1);
    if ((bo & 4) == 0) {
        const Value ctr = e_.ins().iadd_imm(e_.load_spr(eSPR::CTR), -1);
        e_.store_spr(eSPR::CTR, ctr);
        ctr_ok = e_.ins().icmp((bo & 2) ? IntCC::CL_INTCC_EQUAL : IntCC::CL_INTCC_NOT_EQUAL, ctr, e_.i64(0));
    }

    Value cond_ok = e_.i8(1);
    if ((bo & 16) == 0)
        cond_ok
            = e_.ins().icmp(IntCC::CL_INTCC_EQUAL, e_.get_cr_field(bi >> 2, bi & 3), e_.i8((bo & 8) != 0));

    if (lk)
        e_.store_spr(eSPR::LR, e_.i64(info_.mAddress + 4));

    const Block taken = e_.builder.create_block();
    const Block skipped = e_.builder.create_block();
    e_.Branch(e_.ins().band(ctr_ok, cond_ok), taken, {}, skipped, {});
    e_.SwitchToBlock(taken);
    branch_indirect(e_, info_, target, lk);

    if (!e_.terminated)
        e_.Jump(skipped);

    e_.SwitchToBlock(skipped);
}

CLHandler(bcctr) {
    const auto bo = info_.mInst.field_bo();
    const auto bi = info_.mInst.field_bi();
    const bool lk = info_.mInst.field_lk();

    Value cond_ok = e_.i8(1);
    if ((bo & 0x10) == 0)
        cond_ok
            = e_.ins().icmp(IntCC::CL_INTCC_EQUAL, e_.get_cr_field(bi >> 2, bi & 3), e_.i8((bo & 0x8) != 0));

    const Block taken = e_.builder.create_block();
    const Block skipped = e_.builder.create_block();

    e_.Branch(cond_ok, taken, {}, skipped, {});
    e_.SwitchToBlock(taken);

    const Value target = e_.ins().ireduce(types::I32(), e_.load_spr(eSPR::CTR));

    if (lk)
        e_.store_spr(eSPR::LR, e_.i64(info_.mAddress + 4));

    branch_indirect(e_, info_, target, lk);

    if (!e_.terminated)
        e_.Jump(skipped);

    e_.SwitchToBlock(skipped);
}

// TODO: same as below
CLHandler(tdi) {}

// TODO: finish implementation, and hook host handler, for now just assume traps are ignored
CLHandler(twi) {
    // const auto to = info_.mInst.field_to();  // or field_rt()
    // const auto ra = info_.mInst.field_ra();
    // const auto simm = info_.mInst.field_simm();
    //
    // const int32_t imm = sign_extend<16>(simm);
    //
    //// special software trap
    //// twi 31, r0, imm
    // if (ra == 0 && to == 0x1F) {
    //     e_.emit_trap(static_cast<uint16_t>(imm));
    //     return;
    // }
    //
    // if (to == 0)
    //    return;
    //
    // const Value a = e_.ins().ireduce(types::I32(), e_.load_gpr(ra));
    //
    // Value trap = e_.i8(0);
    //
    //// signed <
    // if (to & 0x10)
    //     trap = e_.ins().bor(trap, e_.ins().icmp_imm_s(IntCC::CL_INTCC_SIGNED_LESS_THAN, a, imm));
    //
    //// signed >
    // if (to & 0x08)
    //     trap = e_.ins().bor(trap, e_.ins().icmp_imm_s(IntCC::CL_INTCC_SIGNED_GREATER_THAN, a, imm));
    //
    //// ==
    // if (to & 0x04)
    //     trap = e_.ins().bor(trap, e_.ins().icmp_imm_s(IntCC::CL_INTCC_EQUAL, a, imm));
    //
    //// unsigned <
    // if (to & 0x02)
    //     trap = e_.ins().bor(
    //         trap, e_.ins().icmp_imm_u(IntCC::CL_INTCC_UNSIGNED_LESS_THAN, a, static_cast<uint32_t>(imm)));
    //
    //// unsigned >
    // if (to & 0x01)
    //     trap = e_.ins().bor(
    //         trap, e_.ins().icmp_imm_u(IntCC::CL_INTCC_UNSIGNED_GREATER_THAN, a,
    //         static_cast<uint32_t>(imm)));
    //
    // e_.emit_trap_if(trap);
}

CLHandler(mftb) {
    const auto rd = info_.mInst.field_rd();
    const auto tbr = info_.mInst.field_tbr();

    Value time = e_.load_clock();

    if (tbr == 269) {
        time = e_.ins().ushr_imm_u(time, 32);
    }

    e_.store_gpr(rd, time);
}

CLHandler(mfmsr) {
    const auto rt = info_.mInst.field_rd();
    const Value msr = e_.load_msr();

    e_.store_gpr(rt, msr);
}

CLHandler(mtmsr) {
    const auto rs = info_.mInst.field_rs();

    e_.store_msr(e_.load_gpr(rs));
}

// from QEMU
#define PPC_BIT_NR(bit) (63 - (bit))

#define MSR_HV PPC_BIT_NR(3)
#define MSR_S PPC_BIT_NR(41)
#define MSR_EE PPC_BIT_NR(48)
#define MSR_ME PPC_BIT_NR(51)
#define MSR_RI PPC_BIT_NR(62)
#define MSR_LE PPC_BIT_NR(63)

CLHandler(mtmsrd) {
    const auto rs = info_.mInst.field_rs();
    const auto l = info_.mInst.field_mtmsrd_l();

    if (l == 0) {
        LOG_DEBUG("UNIMPLEMENTED mtmsrd_l == 0\n");
        throw std::runtime_error("UNIMPLEMENTED mtmsrd_l == 0\n");
        return;
    }

    const Value from = e_.load_gpr(rs);
    const Value msr = e_.load_msr();

    // EE = architectural bit 48 -> LSB bit 15
    // RI = architectural bit 62 -> LSB bit 1
    constexpr uint64_t mtmsrd_mask = (1ULL << 15) | (1ULL << 1);
    const Value new_msr = e_.ins().bitselect(e_.i64(mtmsrd_mask), from, msr);

    e_.store_msr(new_msr);
}
