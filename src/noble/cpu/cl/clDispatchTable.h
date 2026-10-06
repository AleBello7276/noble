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
    table[to_underlying(PpcOpcode::Twi)] = &cl_twi_handler;
    table[to_underlying(PpcOpcode::Mftb)] = &cl_mftb_handler;
    table[to_underlying(PpcOpcode::Mfmsr)] = &cl_mfmsr_handler;
    table[to_underlying(PpcOpcode::Mtmsr)] = &cl_mtmsr_handler;
    table[to_underlying(PpcOpcode::Mtmsrd)] = &cl_mtmsrd_handler;

    table[to_underlying(PpcOpcode::Add)] = &cl_add_handler;
    table[to_underlying(PpcOpcode::Addze)] = &cl_addze_handler;
    table[to_underlying(PpcOpcode::Addis)] = &cl_addis_handler;
    table[to_underlying(PpcOpcode::Addi)] = &cl_addi_handler;
    table[to_underlying(PpcOpcode::Addic)] = &cl_addic_handler;
    table[to_underlying(PpcOpcode::Addic_)] = &cl_addic_handler;
    table[to_underlying(PpcOpcode::Subf)] = &cl_subf_handler;
    table[to_underlying(PpcOpcode::Subfic)] = &cl_subfic_handler;
    table[to_underlying(PpcOpcode::Subfe)] = &cl_subfe_handler;
    table[to_underlying(PpcOpcode::Subfc)] = &cl_subfc_handler;
    table[to_underlying(PpcOpcode::And)] = &cl_and__handler;
    table[to_underlying(PpcOpcode::Andc)] = &cl_andc_handler;
    table[to_underlying(PpcOpcode::Andi_)] = &cl_andi_handler;
    table[to_underlying(PpcOpcode::Or)] = &cl_or__handler;
    table[to_underlying(PpcOpcode::Orc)] = &cl_orc_handler;
    table[to_underlying(PpcOpcode::Ori)] = &cl_ori_handler;
    table[to_underlying(PpcOpcode::Oris)] = &cl_oris_handler;
    table[to_underlying(PpcOpcode::Cmp)] = &cl_cmp_handler;
    table[to_underlying(PpcOpcode::Cmpi)] = &cl_cmpi_handler;
    table[to_underlying(PpcOpcode::Cmpli)] = &cl_cmpli_handler;
    table[to_underlying(PpcOpcode::Cmpl)] = &cl_cmpl_handler;
    table[to_underlying(PpcOpcode::Extsb)] = &cl_extsb_handler;
    table[to_underlying(PpcOpcode::Extsw)] = &cl_extsw_handler;
    table[to_underlying(PpcOpcode::Cntlzw)] = &cl_cntlzw_handler;
    table[to_underlying(PpcOpcode::Srw)] = &cl_srw_handler;
    table[to_underlying(PpcOpcode::Srd)] = &cl_srd_handler;
    table[to_underlying(PpcOpcode::Srawi)] = &cl_srawi_handler;
    table[to_underlying(PpcOpcode::Slw)] = &cl_slw_handler;
    table[to_underlying(PpcOpcode::Rlwinm)] = &cl_rlwinm_handler;
    table[to_underlying(PpcOpcode::Rlwimi)] = &cl_rlwimi_handler;
    table[to_underlying(PpcOpcode::Divwu)] = &cl_divwu_handler;
    table[to_underlying(PpcOpcode::Divw)] = &cl_divw_handler;
    table[to_underlying(PpcOpcode::Mullw)] = &cl_mullw_handler;
    table[to_underlying(PpcOpcode::Mulli)] = &cl_mulli_handler;
    table[to_underlying(PpcOpcode::Xor)] = &cl_xor__handler;
    table[to_underlying(PpcOpcode::Xori)] = &cl_xori_handler;
    table[to_underlying(PpcOpcode::Xoris)] = &cl_xoris_handler;
    table[to_underlying(PpcOpcode::Nor)] = &cl_nor_handler;
    table[to_underlying(PpcOpcode::Rldicl)] = &cl_rldicl_handler;
    table[to_underlying(PpcOpcode::Rldicr)] = &cl_rldicr_handler;
    table[to_underlying(PpcOpcode::Neg)] = &cl_neg_handler;
    table[to_underlying(PpcOpcode::Eqv)] = &cl_eqv_handler;

    table[to_underlying(PpcOpcode::Fmul)] = &cl_fmul_handler;
    table[to_underlying(PpcOpcode::Fmuls)] = &cl_fmuls_handler;
    table[to_underlying(PpcOpcode::Fcfid)] = &cl_fcfid_handler;
    table[to_underlying(PpcOpcode::Fdiv)] = &cl_fdiv_handler;
    table[to_underlying(PpcOpcode::Fdivs)] = &cl_fdivs_handler;
    table[to_underlying(PpcOpcode::Fmr)] = &cl_fmr_handler;
    table[to_underlying(PpcOpcode::Fabs)] = &cl_fabs_handler;
    table[to_underlying(PpcOpcode::Fadd)] = &cl_fadd_handler;
    table[to_underlying(PpcOpcode::Fadds)] = &cl_fadds_handler;
    table[to_underlying(PpcOpcode::Fctid)] = &cl_fctid_handler;
    table[to_underlying(PpcOpcode::Fctidz)] = &cl_fctidz_handler;
    table[to_underlying(PpcOpcode::Fsub)] = &cl_fsub_handler;
    table[to_underlying(PpcOpcode::Fsubs)] = &cl_fsubs_handler;
    table[to_underlying(PpcOpcode::Fnmsub)] = &cl_fnmsub_handler;
    table[to_underlying(PpcOpcode::Fnmsubs)] = &cl_fnmsubs_handler;
    table[to_underlying(PpcOpcode::Fmadd)] = &cl_fmadd_handler;
    table[to_underlying(PpcOpcode::Fmadds)] = &cl_fmadds_handler;
    table[to_underlying(PpcOpcode::Fneg)] = &cl_fneg_handler;
    table[to_underlying(PpcOpcode::Fcmpo)] = &cl_fcmpo_handler;
    table[to_underlying(PpcOpcode::Fcmpu)] = &cl_fcmpu_handler;
    table[to_underlying(PpcOpcode::Fsel)] = &cl_fsel_handler;
    table[to_underlying(PpcOpcode::Fsqrt)] = &cl_fsqrt_handler;
    table[to_underlying(PpcOpcode::Fsqrts)] = &cl_fsqrts_handler;
    table[to_underlying(PpcOpcode::Fctiw)] = &cl_fctiw_handler;
    table[to_underlying(PpcOpcode::Fctiwz)] = &cl_fctiwz_handler;
    table[to_underlying(PpcOpcode::Frsp)] = &cl_frsp_handler;
    table[to_underlying(PpcOpcode::Fmsub)] = &cl_fmsub_handler;
    table[to_underlying(PpcOpcode::Fmsubs)] = &cl_fmsubs_handler;

    table[to_underlying(PpcOpcode::Dcbt)] = &cl_dcbt_handler;
    table[to_underlying(PpcOpcode::Dcbtst)] = &cl_dcbtst_handler;

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

    // load / store floating single precision
    table[to_underlying(PpcOpcode::Lfs)] = &cl_lfs_handler;
    table[to_underlying(PpcOpcode::Lfsu)] = &cl_lfsu_handler;
    table[to_underlying(PpcOpcode::Lfsx)] = &cl_lfsx_handler;
    table[to_underlying(PpcOpcode::Lfsux)] = &cl_lfsux_handler;
    table[to_underlying(PpcOpcode::Stfs)] = &cl_stfs_handler;
    table[to_underlying(PpcOpcode::Stfsu)] = &cl_stfsu_handler;
    table[to_underlying(PpcOpcode::Stfsx)] = &cl_stfsx_handler;
    table[to_underlying(PpcOpcode::Stfsux)] = &cl_stfsux_handler;

    // load / store floating double precision
    table[to_underlying(PpcOpcode::Lfd)] = &cl_lfd_handler;
    table[to_underlying(PpcOpcode::Lfdu)] = &cl_lfdu_handler;
    table[to_underlying(PpcOpcode::Lfdx)] = &cl_lfdx_handler;
    table[to_underlying(PpcOpcode::Lfdux)] = &cl_lfdux_handler;
    table[to_underlying(PpcOpcode::Stfd)] = &cl_stfd_handler;
    table[to_underlying(PpcOpcode::Stfdu)] = &cl_stfdu_handler;
    table[to_underlying(PpcOpcode::Stfdx)] = &cl_stfdx_handler;
    table[to_underlying(PpcOpcode::Stfdux)] = &cl_stfdux_handler;

    table[to_underlying(PpcOpcode::Lwarx)] = &cl_lwarx_handler;
    table[to_underlying(PpcOpcode::Stwcx_)] = &cl_stwcx_handler;
    table[to_underlying(PpcOpcode::Stfiwx)] = &cl_stfiwx_handler;

    table[to_underlying(PpcOpcode::Eieio)] = &cl_eieio_handler;
    table[to_underlying(PpcOpcode::Sync)] = &cl_sync_handler;
    table[to_underlying(PpcOpcode::Isync)] = &cl_isync_handler;

    table[to_underlying(PpcOpcode::Dcbz)] = &cl_dcbz_handler;
    table[to_underlying(PpcOpcode::Dcbzl)] = &cl_dcbzl_handler;

    // vxu
    table[to_underlying(PpcOpcode::Stvx)] = &cl_stvx_handler;
    table[to_underlying(PpcOpcode::Stvxl)] = &cl_stvxl_handler;
    table[to_underlying(PpcOpcode::Stvx128)] = &cl_stvx128_handler;
    table[to_underlying(PpcOpcode::Stvxl128)] = &cl_stvxl128_handler;

    table[to_underlying(PpcOpcode::Lvx)] = &cl_lvx_handler;
    table[to_underlying(PpcOpcode::Lvxl)] = &cl_lvxl_handler;
    table[to_underlying(PpcOpcode::Lvx128)] = &cl_lvx128_handler;
    table[to_underlying(PpcOpcode::Lvxl128)] = &cl_lvxl128_handler;

    table[to_underlying(PpcOpcode::Stvlx)] = &cl_stvlx_handler;
    table[to_underlying(PpcOpcode::Stvlxl)] = &cl_stvlxl_handler;
    table[to_underlying(PpcOpcode::Stvlx128)] = &cl_stvlx128_handler;
    table[to_underlying(PpcOpcode::Stvlxl128)] = &cl_stvlxl128_handler;

    table[to_underlying(PpcOpcode::Stvrx)] = &cl_stvrx_handler;
    table[to_underlying(PpcOpcode::Stvrxl)] = &cl_stvrxl_handler;
    table[to_underlying(PpcOpcode::Stvrx128)] = &cl_stvrx128_handler;
    table[to_underlying(PpcOpcode::Stvrxl128)] = &cl_stvrxl128_handler;

    table[to_underlying(PpcOpcode::Vspltisw)] = &cl_vspltisw_handler;
    table[to_underlying(PpcOpcode::Vspltisw128)] = &cl_vspltisw128_handler;
    table[to_underlying(PpcOpcode::Vupkd3d128)] = &cl_vupkd3d128_handler;

    table[to_underlying(PpcOpcode::Vpermwi128)] = &cl_vpermwi128_handler;

    table[to_underlying(PpcOpcode::Vsubfp)] = &cl_vsubfp_handler;
    table[to_underlying(PpcOpcode::Vsubfp128)] = &cl_vsubfp128_handler;

    table[to_underlying(PpcOpcode::Vor)] = &cl_vor_handler;
    table[to_underlying(PpcOpcode::Vor128)] = &cl_vor128_handler;

    table[to_underlying(PpcOpcode::Vrlimi128)] = &cl_vrlimi128_handler;

    table[to_underlying(PpcOpcode::Lvsl)] = &cl_lvsl_handler;
    table[to_underlying(PpcOpcode::Lvsl128)] = &cl_lvsl128_handler;

    table[to_underlying(PpcOpcode::Vperm)] = &cl_vperm_handler;
    table[to_underlying(PpcOpcode::Vperm128)] = &cl_vperm128_handler;

    return table;
}();
