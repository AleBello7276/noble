#pragma once

#include "emit/cl_emitters.h"
#include "powerpc-rs.h"

using namespace std;  // only for to_underlying honestly
inline constexpr auto emitter_dispatch_table = [] {
    std::array<EmitterHandler, to_underlying(PpcOpcode::Count)> table{};
    table.fill(&cl_illegal_handler);  // init

    table[to_underlying(PpcOpcode::Mfspr)] = &cl_mfspr_handler;
    table[to_underlying(PpcOpcode::Mtspr)] = &cl_mtspr_handler;

    table[to_underlying(PpcOpcode::B)] = &cl_b_handler;
    table[to_underlying(PpcOpcode::Bc)] = &cl_bc_handler;
    table[to_underlying(PpcOpcode::Bclr)] = &cl_bclr_handler;
    table[to_underlying(PpcOpcode::Bcctr)] = &cl_bcctr_handler;

    table[to_underlying(PpcOpcode::Add)] = &cl_add_handler;
    table[to_underlying(PpcOpcode::Addis)] = &cl_addis_handler;
    table[to_underlying(PpcOpcode::Addi)] = &cl_addi_handler;
    table[to_underlying(PpcOpcode::Addic)] = &cl_addic_handler;
    table[to_underlying(PpcOpcode::Addic_)] = &cl_addic_handler;
    table[to_underlying(PpcOpcode::Subfe)] = &cl_subfe_handler;
    table[to_underlying(PpcOpcode::Or)] = &cl_or__handler;
    table[to_underlying(PpcOpcode::Ori)] = &cl_ori_handler;
    table[to_underlying(PpcOpcode::Cmpi)] = &cl_cmpi_handler;
    table[to_underlying(PpcOpcode::Cmpli)] = &cl_cmpli_handler;
    table[to_underlying(PpcOpcode::Cmpl)] = &cl_cmpl_handler;
    table[to_underlying(PpcOpcode::Extsb)] = &cl_extsb_handler;
    table[to_underlying(PpcOpcode::Cntlzw)] = &cl_cntlzw_handler;
    table[to_underlying(PpcOpcode::Rlwinm)] = &cl_rlwinm_handler;

    // byte load/store
    table[to_underlying(PpcOpcode::Lbz)] = &cl_lbz_handler;
    table[to_underlying(PpcOpcode::Lbzu)] = &cl_lbzu_handler;
    table[to_underlying(PpcOpcode::Lbzx)] = &cl_lbzx_handler;
    table[to_underlying(PpcOpcode::Lbzux)] = &cl_lbzux_handler;
    table[to_underlying(PpcOpcode::Stb)] = &cl_stb_handler;
    table[to_underlying(PpcOpcode::Stbu)] = &cl_stbu_handler;
    table[to_underlying(PpcOpcode::Stbx)] = &cl_stbx_handler;
    table[to_underlying(PpcOpcode::Stbux)] = &cl_stbux_handler;

    // half word load/store
    table[to_underlying(PpcOpcode::Lhz)] = &cl_lhz_handler;
    table[to_underlying(PpcOpcode::Lhzu)] = &cl_lhzu_handler;
    table[to_underlying(PpcOpcode::Lhzx)] = &cl_lhzx_handler;
    table[to_underlying(PpcOpcode::Lhzux)] = &cl_lhzux_handler;
    table[to_underlying(PpcOpcode::Sth)] = &cl_sth_handler;
    table[to_underlying(PpcOpcode::Sthu)] = &cl_sthu_handler;
    table[to_underlying(PpcOpcode::Sthx)] = &cl_sthx_handler;
    table[to_underlying(PpcOpcode::Sthux)] = &cl_sthux_handler;

    // word load/store
    table[to_underlying(PpcOpcode::Lwz)] = &cl_lwz_handler;
    table[to_underlying(PpcOpcode::Lwzu)] = &cl_lwzu_handler;
    table[to_underlying(PpcOpcode::Lwzx)] = &cl_lwzx_handler;
    table[to_underlying(PpcOpcode::Lwzux)] = &cl_lwzux_handler;
    table[to_underlying(PpcOpcode::Stw)] = &cl_stw_handler;
    table[to_underlying(PpcOpcode::Stwu)] = &cl_stwu_handler;
    table[to_underlying(PpcOpcode::Stwx)] = &cl_stwx_handler;
    table[to_underlying(PpcOpcode::Stwux)] = &cl_stwux_handler;

    // double word load/store
    table[to_underlying(PpcOpcode::Ld)] = &cl_ld_handler;
    table[to_underlying(PpcOpcode::Ldu)] = &cl_ldu_handler;
    table[to_underlying(PpcOpcode::Ldx)] = &cl_ldx_handler;
    table[to_underlying(PpcOpcode::Ldux)] = &cl_ldux_handler;
    table[to_underlying(PpcOpcode::Std)] = &cl_std_handler;
    table[to_underlying(PpcOpcode::Stdu)] = &cl_stdu_handler;
    table[to_underlying(PpcOpcode::Stdx)] = &cl_stdx_handler;
    table[to_underlying(PpcOpcode::Stdux)] = &cl_stdux_handler;

    return table;
}();
