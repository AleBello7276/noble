#pragma once

#include "ppc_opcode.h"
#include <stdbool.h>
#include <stdint.h>

namespace codec {

#ifdef __cplusplus
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
} PpcParsedIns;

typedef struct {
    uint32_t code;
    PpcOpcode opcode;
} PpcIns;

typedef struct {
    uint8_t valid;
    uint32_t address;
} PpcBranchDest;

// Decode a 32-bit instruction with Xenon extensions.
PpcIns ppc_ins_new(uint32_t code);

// Return the instruction's original mnemonic and operands.
PpcParsedIns ppc_ins_basic(PpcIns ins);

// Return its simplified mnemonic and operands.
PpcParsedIns ppc_ins_simplified(PpcIns ins);

// Return registers written by the instruction.
PpcArguments ppc_ins_defs(PpcIns ins);

// Return registers read by the instruction.
PpcArguments ppc_ins_uses(PpcIns ins);

// Report whether the instruction branches.
bool ppc_ins_is_branch(PpcIns ins);

// Resolve a direct branch target from its instruction address.
PpcBranchDest ppc_ins_branch_dest(PpcIns ins, uint32_t address);

#ifdef __cplusplus
}
#endif

}  // namespace codec
