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
