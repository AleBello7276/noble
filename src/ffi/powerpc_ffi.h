#pragma once

#include "ppc_opcode.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
namespace codec {
extern "C" {
#endif

typedef enum PpcArgumentKind {
    PPC_NONE,
    PPC_GPR,
    PPC_FPR,
    PPC_SR,
    PPC_SPR,
    PPC_CR_FIELD,
    PPC_CR_BIT,
    PPC_GQR,
    PPC_UIMM,
    PPC_SIMM,
    PPC_OFFSET,
    PPC_BRANCH_DEST,
    PPC_OPAQUE_U,
    PPC_VR
} PpcArgumentKind;

typedef union {
    uint8_t gpr;
    uint8_t fpr;
    uint8_t sr;
    uint16_t spr;
    uint8_t cr_field;
    uint8_t cr_bit;
    uint8_t gqr;
    uint16_t uimm;
    int16_t simm;
    int16_t offset;
    int32_t branch_dest;
    uint16_t opaque_u;
    uint8_t vr;
} PpcArgumentValue;

typedef struct {
    PpcArgumentKind kind;
    PpcArgumentValue value;
} PpcArgument;

typedef struct {
    uint8_t count;
    PpcArgument items[5];
} PpcArguments;

typedef struct {
    char mnemonic[32];
    PpcArguments args;
    char text[128];
} PpcParsedIns;

typedef struct {
    uint32_t code;
    PpcOpcode opcode;
    uint32_t extensions;
} PpcIns;

typedef struct {
    uint8_t valid;
    uint32_t address;
} PpcBranchDest;

typedef struct {
    uint8_t valid;
    int32_t offset;
} PpcBranchOffset;

// decode a 32-bit instruction with xenon extensions
PpcIns ppc_ins_new(uint32_t code);

// decode a 32-bit instruction with the requested extension bitmask
PpcIns ppc_ins_new_with_extensions(uint32_t code, uint32_t extensions);

// return the xenon extension bitmask
uint32_t ppc_extensions_xenon(void);

// return the gekko and broadway extension bitmask
uint32_t ppc_extensions_gekko_broadway(void);

// return the original mnemonic operands and formatted text
PpcParsedIns ppc_ins_basic(PpcIns ins);

// return the simplified mnemonic operands and formatted text
PpcParsedIns ppc_ins_simplified(PpcIns ins);

// return registers written by the instruction
PpcArguments ppc_ins_defs(PpcIns ins);

// return registers read by the instruction
PpcArguments ppc_ins_uses(PpcIns ins);

// report whether the instruction branches
bool ppc_ins_is_branch(PpcIns ins);

// report whether the instruction has a direct branch destination
bool ppc_ins_is_direct_branch(PpcIns ins);

// report whether the branch is unconditional
bool ppc_ins_is_unconditional_branch(PpcIns ins);

// report whether the branch is conditional
bool ppc_ins_is_conditional_branch(PpcIns ins);

// report whether the instruction is the blr encoding
bool ppc_ins_is_blr(PpcIns ins);

// return the relative offset of a direct branch when present
PpcBranchOffset ppc_ins_branch_offset(PpcIns ins);

// resolve a direct branch target from its instruction address
PpcBranchDest ppc_ins_branch_dest(PpcIns ins, uint32_t address);

// return raw fields from the encoded instruction
int16_t ppc_ins_field_simm(PpcIns ins);
uint16_t ppc_ins_field_uimm(PpcIns ins);
int16_t ppc_ins_field_offset(PpcIns ins);
uint8_t ppc_ins_field_bo(PpcIns ins);
uint8_t ppc_ins_field_bi(PpcIns ins);
int16_t ppc_ins_field_bd(PpcIns ins);
int32_t ppc_ins_field_li(PpcIns ins);
uint8_t ppc_ins_field_sh(PpcIns ins);
uint8_t ppc_ins_field_mb(PpcIns ins);
uint8_t ppc_ins_field_me(PpcIns ins);
uint8_t ppc_ins_field_rs(PpcIns ins);
uint8_t ppc_ins_field_rd(PpcIns ins);
uint8_t ppc_ins_field_ra(PpcIns ins);
uint8_t ppc_ins_field_rb(PpcIns ins);
uint8_t ppc_ins_field_sr(PpcIns ins);
uint16_t ppc_ins_field_spr(PpcIns ins);
uint8_t ppc_ins_field_frs(PpcIns ins);
uint8_t ppc_ins_field_frd(PpcIns ins);
uint8_t ppc_ins_field_fra(PpcIns ins);
uint8_t ppc_ins_field_frb(PpcIns ins);
uint8_t ppc_ins_field_frc(PpcIns ins);
uint8_t ppc_ins_field_crbd(PpcIns ins);
uint8_t ppc_ins_field_crba(PpcIns ins);
uint8_t ppc_ins_field_crbb(PpcIns ins);
uint8_t ppc_ins_field_crfd(PpcIns ins);
uint8_t ppc_ins_field_crfs(PpcIns ins);
uint8_t ppc_ins_field_crm(PpcIns ins);
uint8_t ppc_ins_field_nb(PpcIns ins);
uint16_t ppc_ins_field_tbr(PpcIns ins);
uint8_t ppc_ins_field_mtfsf_fm(PpcIns ins);
uint8_t ppc_ins_field_mtfsf_imm(PpcIns ins);
uint8_t ppc_ins_field_spr_sprg(PpcIns ins);
uint8_t ppc_ins_field_spr_bat(PpcIns ins);
uint8_t ppc_ins_field_to(PpcIns ins);
uint8_t ppc_ins_field_l(PpcIns ins);
uint8_t ppc_ins_field_sync_l(PpcIns ins);
int16_t ppc_ins_field_ds(PpcIns ins);
uint8_t ppc_ins_field_sh64(PpcIns ins);
uint8_t ppc_ins_field_mb64(PpcIns ins);
uint8_t ppc_ins_field_me64(PpcIns ins);
uint8_t ppc_ins_field_mtmsrd_l(PpcIns ins);
int16_t ppc_ins_field_ps_offset(PpcIns ins);
uint8_t ppc_ins_field_ps_i(PpcIns ins);
uint8_t ppc_ins_field_ps_ix(PpcIns ins);
uint8_t ppc_ins_field_ps_w(PpcIns ins);
uint8_t ppc_ins_field_ps_wx(PpcIns ins);
int8_t ppc_ins_field_vsimm(PpcIns ins);
uint8_t ppc_ins_field_vuimm(PpcIns ins);
uint8_t ppc_ins_field_vs(PpcIns ins);
uint8_t ppc_ins_field_vd(PpcIns ins);
uint8_t ppc_ins_field_va(PpcIns ins);
uint8_t ppc_ins_field_vb(PpcIns ins);
uint8_t ppc_ins_field_vc(PpcIns ins);
uint8_t ppc_ins_field_ds_a(PpcIns ins);
uint8_t ppc_ins_field_strm(PpcIns ins);
uint8_t ppc_ins_field_shb(PpcIns ins);
uint8_t ppc_ins_field_vds128(PpcIns ins);
uint8_t ppc_ins_field_va128(PpcIns ins);
uint8_t ppc_ins_field_vb128(PpcIns ins);
uint8_t ppc_ins_field_vc128(PpcIns ins);
uint8_t ppc_ins_field_perm(PpcIns ins);
uint8_t ppc_ins_field_d3dtype(PpcIns ins);
uint8_t ppc_ins_field_vmask(PpcIns ins);
uint8_t ppc_ins_field_zimm(PpcIns ins);
bool ppc_ins_field_oe(PpcIns ins);
bool ppc_ins_field_rc(PpcIns ins);
bool ppc_ins_field_lk(PpcIns ins);
bool ppc_ins_field_aa(PpcIns ins);
bool ppc_ins_field_bp(PpcIns ins);
bool ppc_ins_field_bnp(PpcIns ins);
bool ppc_ins_field_bp_nd(PpcIns ins);
bool ppc_ins_field_t(PpcIns ins);
bool ppc_ins_field_rcav(PpcIns ins);
bool ppc_ins_field_rc128(PpcIns ins);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
}  // namespace codec
#endif
