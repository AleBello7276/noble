#pragma once

#include "cl_util.h"

CLHandler(dcbt) {
    // stub, ingore
}

CLHandler(dcbtst) {
    // stub, ignore
}

/*
    Load instructions
 */

// byte
CLHandler(lbz) {
    emit_load<8>(e_, info_);
}

CLHandler(lbzu) {
    emit_load<8, true>(e_, info_);
}

CLHandler(lbzx) {
    emit_load_indexed<8>(e_, info_);
}

CLHandler(lbzux) {
    emit_load_indexed<8, true>(e_, info_);
}

// halfword
CLHandler(lhz) {
    emit_load<16>(e_, info_);
}

CLHandler(lhzu) {
    emit_load<16, true>(e_, info_);
}

CLHandler(lhzx) {
    emit_load_indexed<16>(e_, info_);
}

CLHandler(lhzux) {
    emit_load_indexed<16, true>(e_, info_);
}

// word
CLHandler(lwz) {
    emit_load<32>(e_, info_);
}

CLHandler(lwzu) {
    emit_load<32, true>(e_, info_);
}

CLHandler(lwzx) {
    emit_load_indexed<32>(e_, info_);
}

CLHandler(lwzux) {
    emit_load_indexed<32, true>(e_, info_);
}

// doubleword
CLHandler(ld) {
    emit_load<64>(e_, info_);
}

CLHandler(ldu) {
    emit_load<64, true>(e_, info_);
}

CLHandler(ldx) {
    emit_load_indexed<64>(e_, info_);
}

CLHandler(ldux) {
    emit_load_indexed<64, true>(e_, info_);
}

/*
    Store instructions
*/

// byte
CLHandler(stb) {
    emit_store<8>(e_, info_);
}

CLHandler(stbu) {
    emit_store<8, true>(e_, info_);
}

CLHandler(stbx) {
    emit_store_indexed<8>(e_, info_);
}

CLHandler(stbux) {
    emit_store_indexed<8, true>(e_, info_);
}

// halfword
CLHandler(sth) {
    emit_store<16>(e_, info_);
}

CLHandler(sthu) {
    emit_store<16, true>(e_, info_);
}

CLHandler(sthx) {
    emit_store_indexed<16>(e_, info_);
}

CLHandler(sthux) {
    emit_store_indexed<16, true>(e_, info_);
}

// word
CLHandler(stw) {
    emit_store<32>(e_, info_);
}

CLHandler(stwu) {
    emit_store<32, true>(e_, info_);
}

CLHandler(stwx) {
    emit_store_indexed<32>(e_, info_);
}

CLHandler(stwux) {
    emit_store_indexed<32, true>(e_, info_);
}

// doubleword
CLHandler(std) {
    emit_store<64>(e_, info_);
}

CLHandler(stdu) {
    emit_store<64, true>(e_, info_);
}

CLHandler(stdx) {
    emit_store_indexed<64>(e_, info_);
}

CLHandler(stdux) {
    emit_store_indexed<64, true>(e_, info_);
}

// single
CLHandler(lfs) {
    emit_fload<32>(e_, info_);
}

CLHandler(lfsu) {
    emit_fload<32, true>(e_, info_);
}

CLHandler(lfsx) {
    emit_fload_indexed<32>(e_, info_);
}

CLHandler(lfsux) {
    emit_fload_indexed<32, true>(e_, info_);
}

// double
CLHandler(lfd) {
    emit_fload<64>(e_, info_);
}

CLHandler(lfdu) {
    emit_fload<64, true>(e_, info_);
}

CLHandler(lfdx) {
    emit_fload_indexed<64>(e_, info_);
}

CLHandler(lfdux) {
    emit_fload_indexed<64, true>(e_, info_);
}

// single
CLHandler(stfs) {
    emit_fstore<32>(e_, info_);
}

CLHandler(stfsu) {
    emit_fstore<32, true>(e_, info_);
}

CLHandler(stfsx) {
    emit_fstore_indexed<32>(e_, info_);
}

CLHandler(stfsux) {
    emit_fstore_indexed<32, true>(e_, info_);
}

// double
CLHandler(stfd) {
    emit_fstore<64>(e_, info_);
}

CLHandler(stfdu) {
    emit_fstore<64, true>(e_, info_);
}

CLHandler(stfdx) {
    emit_fstore_indexed<64>(e_, info_);
}

CLHandler(stfdux) {
    emit_fstore_indexed<64, true>(e_, info_);
}

CLHandler(stfiwx) {
    const auto frs = info_.mInst.field_frs();
    const auto ra = info_.mInst.field_ra();
    const auto rb = info_.mInst.field_rb();

    const Value base = ra ? e_.load_gpr(ra) : e_.i64(0);
    const Value ea = e_.ins().iadd(base, e_.load_gpr(rb));

    const Value bits = e_.ins().bitcast(types::I64(), MemFlags{}, e_.load_fpr(frs));
    const Value value = e_.ins().bswap(e_.reduce32(bits));

    e_.store_memory(ea, value);
}

CLHandler(lwarx) {
    const auto rt = info_.mInst.field_rd();
    const auto ra = info_.mInst.field_ra();
    const auto rb = info_.mInst.field_rb();

    const Value base = ra ? e_.load_gpr(ra) : e_.i64(0);
    const Value ea = e_.ins().iadd(base, e_.load_gpr(rb));

    e_.ins().fence();

    const Value raw = e_.load_memory(ea, types::I32());

    e_.ins().store(e_.builder.memflags_new(), e_.reduce32(ea), e_.vCpuState, ReserveAddressOffset());
    e_.ins().store(e_.builder.memflags_new(), raw, e_.vCpuState, ReserveValueOffset());
    e_.ins().store(e_.builder.memflags_new(), e_.i8(1), e_.vCpuState, ReserveValidOffset());

    e_.store_gpr(rt, e_.zext64(e_.ins().bswap(raw)));
}

// TODO: refactor this please lol
CLHandler(stwcx) {
    const auto rs = info_.mInst.field_rs();
    const auto ra = info_.mInst.field_ra();
    const auto rb = info_.mInst.field_rb();

    // test
    // e_.ins().store(e_.builder.memflags_new(), e_.i32(info_.mAddress), e_.vCpuState,
    //               static_cast<std::int32_t>(offsetof(PPCContext, CIA)));

    const Value base = ra ? e_.load_gpr(ra) : e_.i64(0);
    const Value ea = e_.reduce32(e_.ins().iadd(base, e_.load_gpr(rb)));
    const Value guest_addr = e_.zext64(ea);
    const Value host_addr = e_.ins().iadd(e_.vMemBase, guest_addr);

    const Value reserve_valid
        = e_.ins().load(types::I8(), e_.builder.memflags_new(), e_.vCpuState, ReserveValidOffset());

    const Value reserve_addr
        = e_.ins().load(types::I32(), e_.builder.memflags_new(), e_.vCpuState, ReserveAddressOffset());

    e_.ins().store(e_.builder.memflags_new(), e_.i8(0), e_.vCpuState, ReserveValidOffset());

    const Value address_matches = e_.ins().icmp(IntCC::CL_INTCC_EQUAL, ea, reserve_addr);
    const Value can_store = e_.ins().band(reserve_valid, address_matches);

    const Block try_store = e_.builder.create_block();
    const Block failed = e_.builder.create_block();
    const Block done = e_.builder.create_block();

    e_.Branch(can_store, try_store, {}, failed, {});

    e_.SwitchToBlock(try_store);

    const Value expected
        = e_.ins().load(types::I32(), e_.builder.memflags_new(), e_.vCpuState, ReserveValueOffset());
    const Value value = e_.reduce32(e_.load_gpr(rs));
    const Value desired = e_.ins().bswap(value);

    const Value old = e_.ins().atomic_cas(e_.builder.memflags_new(), host_addr, expected, desired);
    const Value success = e_.ins().icmp(IntCC::CL_INTCC_EQUAL, old, expected);

    e_.ins().store(e_.builder.memflags_new(), success, e_.vCpuState, CRFieldBitOffset(0, 2));
    e_.Jump(done);

    e_.SwitchToBlock(failed);

    e_.ins().store(e_.builder.memflags_new(), e_.i8(0), e_.vCpuState, CRFieldBitOffset(0, 2));
    e_.Jump(done);

    e_.SwitchToBlock(done);

    // can be a store I16
    e_.ins().store(e_.builder.memflags_new(), e_.i16(0), e_.vCpuState, CRFieldBitOffset(0, 0));
    // e_.ins().store(e_.builder.memflags_new(), e_.i8(0), e_.vCpuState, CRFieldBitOffset(0, 0));
    // e_.ins().store(e_.builder.memflags_new(), e_.i8(0), e_.vCpuState, CRFieldBitOffset(0, 1));

    // TODO:
    /*  const Value so = e_.ins().load(types::I8(), e_.builder.memflags_new(), e_.vCpuState,
      GetXER_SO_Offset()); e_.ins().store(e_.builder.memflags_new(), so, e_.vCpuState, CRFieldBitOffset(0,
      3)); */ // ok clang format..

    e_.ins().fence();
}
