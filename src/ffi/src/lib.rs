use powerpc::{Argument, Arguments, Extensions, Ins, ParsedIns};

mod cranelift;

#[repr(C)]
#[derive(Clone, Copy, Default, Debug, PartialEq, Eq)]
pub enum PpcArgumentKind {
    #[default]
    None,
    Gpr,
    Fpr,
    Sr,
    Spr,
    CrField,
    CrBit,
    Gqr,
    Uimm,
    Simm,
    Offset,
    BranchDest,
    OpaqueU,
    Vr,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub union PpcArgumentValue {
    pub gpr: u8,
    pub fpr: u8,
    pub sr: u8,
    pub spr: u16,
    pub cr_field: u8,
    pub cr_bit: u8,
    pub gqr: u8,
    pub uimm: u16,
    pub simm: i16,
    pub offset: i16,
    pub branch_dest: i32,
    pub opaque_u: u16,
    pub vr: u8,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct PpcArgument {
    pub kind: PpcArgumentKind,
    pub value: PpcArgumentValue,
}

impl Default for PpcArgument {
    fn default() -> Self {
        Self {
            kind: PpcArgumentKind::None,
            value: PpcArgumentValue { branch_dest: 0 },
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct PpcArguments {
    pub count: u8,
    pub items: [PpcArgument; 5],
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct PpcParsedIns {
    pub mnemonic: [u8; 32],
    pub args: PpcArguments,
    pub text: [u8; 128],
}

impl Default for PpcParsedIns {
    fn default() -> Self {
        Self {
            mnemonic: [0; 32],
            args: PpcArguments::default(),
            text: [0; 128],
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct PpcIns {
    pub code: u32,
    pub opcode: u16,
    pub extensions: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct PpcBranchDest {
    pub valid: u8,
    pub address: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct PpcBranchOffset {
    pub valid: u8,
    pub offset: i32,
}

fn argument(arg: Argument) -> PpcArgument {
    use PpcArgumentKind as Kind;
    let (kind, value) = match arg {
        Argument::None => (Kind::None, PpcArgumentValue { branch_dest: 0 }),
        Argument::GPR(x) => (Kind::Gpr, PpcArgumentValue { gpr: x.0 }),
        Argument::FPR(x) => (Kind::Fpr, PpcArgumentValue { fpr: x.0 }),
        Argument::SR(x) => (Kind::Sr, PpcArgumentValue { sr: x.0 }),
        Argument::SPR(x) => (Kind::Spr, PpcArgumentValue { spr: x.0 }),
        Argument::CRField(x) => (Kind::CrField, PpcArgumentValue { cr_field: x.0 }),
        Argument::CRBit(x) => (Kind::CrBit, PpcArgumentValue { cr_bit: x.0 }),
        Argument::GQR(x) => (Kind::Gqr, PpcArgumentValue { gqr: x.0 }),
        Argument::Uimm(x) => (Kind::Uimm, PpcArgumentValue { uimm: x.0 }),
        Argument::Simm(x) => (Kind::Simm, PpcArgumentValue { simm: x.0 }),
        Argument::Offset(x) => (Kind::Offset, PpcArgumentValue { offset: x.0 }),
        Argument::BranchDest(x) => (Kind::BranchDest, PpcArgumentValue { branch_dest: x.0 }),
        Argument::OpaqueU(x) => (Kind::OpaqueU, PpcArgumentValue { opaque_u: x.0 }),
        Argument::VR(x) => (Kind::Vr, PpcArgumentValue { vr: x.0 }),
    };
    PpcArgument { kind, value }
}

fn arguments(args: Arguments) -> PpcArguments {
    let mut result = PpcArguments::default();
    for arg in args {
        if arg == Argument::None {
            break;
        }
        result.items[result.count as usize] = argument(arg);
        result.count += 1;
    }
    result
}

fn name<const N: usize>(text: &str) -> [u8; N] {
    let mut result = [0; N];
    let bytes = text.as_bytes();
    let len = bytes.len().min(result.len() - 1);
    result[..len].copy_from_slice(&bytes[..len]);
    result
}

fn parsed(ins: ParsedIns) -> PpcParsedIns {
    PpcParsedIns {
        mnemonic: name(ins.mnemonic),
        args: arguments(ins.args),
        text: name(&ins.to_string()),
    }
}

fn decode(ins: PpcIns) -> Ins {
    Ins::new(ins.code, Extensions::from_bitmask(ins.extensions))
}

#[no_mangle]
pub extern "C" fn ppc_ins_new(code: u32) -> PpcIns {
    ppc_ins_new_with_extensions(code, Extensions::xenon().bitmask())
}

#[no_mangle]
pub extern "C" fn ppc_ins_new_with_extensions(code: u32, extensions: u32) -> PpcIns {
    let ins = Ins::new(code, Extensions::from_bitmask(extensions));
    PpcIns {
        code,
        opcode: ins.op as u16,
        extensions,
    }
}

#[no_mangle]
pub extern "C" fn ppc_extensions_xenon() -> u32 {
    Extensions::xenon().bitmask()
}

#[no_mangle]
pub extern "C" fn ppc_extensions_gekko_broadway() -> u32 {
    Extensions::gekko_broadway().bitmask()
}

#[no_mangle]
pub extern "C" fn ppc_ins_basic(ins: PpcIns) -> PpcParsedIns {
    parsed(decode(ins).basic())
}

#[no_mangle]
pub extern "C" fn ppc_ins_simplified(ins: PpcIns) -> PpcParsedIns {
    parsed(decode(ins).simplified())
}

#[no_mangle]
pub extern "C" fn ppc_ins_defs(ins: PpcIns) -> PpcArguments {
    arguments(decode(ins).defs())
}

#[no_mangle]
pub extern "C" fn ppc_ins_uses(ins: PpcIns) -> PpcArguments {
    arguments(decode(ins).uses())
}

#[no_mangle]
pub extern "C" fn ppc_ins_is_branch(ins: PpcIns) -> bool {
    decode(ins).is_branch()
}

#[no_mangle]
pub extern "C" fn ppc_ins_is_direct_branch(ins: PpcIns) -> bool {
    decode(ins).is_direct_branch()
}

#[no_mangle]
pub extern "C" fn ppc_ins_is_unconditional_branch(ins: PpcIns) -> bool {
    decode(ins).is_unconditional_branch()
}

#[no_mangle]
pub extern "C" fn ppc_ins_is_conditional_branch(ins: PpcIns) -> bool {
    decode(ins).is_conditional_branch()
}

#[no_mangle]
pub extern "C" fn ppc_ins_is_blr(ins: PpcIns) -> bool {
    decode(ins).is_blr()
}

#[no_mangle]
pub extern "C" fn ppc_ins_branch_offset(ins: PpcIns) -> PpcBranchOffset {
    match decode(ins).branch_offset() {
        Some(offset) => PpcBranchOffset { valid: 1, offset },
        None => PpcBranchOffset::default(),
    }
}

#[no_mangle]
pub extern "C" fn ppc_ins_branch_dest(ins: PpcIns, address: u32) -> PpcBranchDest {
    match decode(ins).branch_dest(address) {
        Some(address) => PpcBranchDest { valid: 1, address },
        None => PpcBranchDest::default(),
    }
}

macro_rules! field {
    ($name:ident, $method:ident, $type:ty) => {
        #[no_mangle]
        pub extern "C" fn $name(ins: PpcIns) -> $type {
            decode(ins).$method()
        }
    };
}

field!(ppc_ins_field_simm, field_simm, i16);
field!(ppc_ins_field_uimm, field_uimm, u16);
field!(ppc_ins_field_offset, field_offset, i16);
field!(ppc_ins_field_bo, field_bo, u8);
field!(ppc_ins_field_bi, field_bi, u8);
field!(ppc_ins_field_bd, field_bd, i16);
field!(ppc_ins_field_li, field_li, i32);
field!(ppc_ins_field_sh, field_sh, u8);
field!(ppc_ins_field_mb, field_mb, u8);
field!(ppc_ins_field_me, field_me, u8);
field!(ppc_ins_field_rs, field_rs, u8);
field!(ppc_ins_field_rd, field_rd, u8);
field!(ppc_ins_field_ra, field_ra, u8);
field!(ppc_ins_field_rb, field_rb, u8);
field!(ppc_ins_field_sr, field_sr, u8);
field!(ppc_ins_field_spr, field_spr, u16);
field!(ppc_ins_field_frs, field_frs, u8);
field!(ppc_ins_field_frd, field_frd, u8);
field!(ppc_ins_field_fra, field_fra, u8);
field!(ppc_ins_field_frb, field_frb, u8);
field!(ppc_ins_field_frc, field_frc, u8);
field!(ppc_ins_field_crbd, field_crbd, u8);
field!(ppc_ins_field_crba, field_crba, u8);
field!(ppc_ins_field_crbb, field_crbb, u8);
field!(ppc_ins_field_crfd, field_crfd, u8);
field!(ppc_ins_field_crfs, field_crfs, u8);
field!(ppc_ins_field_crm, field_crm, u8);
field!(ppc_ins_field_nb, field_nb, u8);
field!(ppc_ins_field_tbr, field_tbr, u16);
field!(ppc_ins_field_mtfsf_fm, field_mtfsf_fm, u8);
field!(ppc_ins_field_mtfsf_imm, field_mtfsf_imm, u8);
field!(ppc_ins_field_spr_sprg, field_spr_sprg, u8);
field!(ppc_ins_field_spr_bat, field_spr_bat, u8);
field!(ppc_ins_field_to, field_to, u8);
field!(ppc_ins_field_l, field_l, u8);
field!(ppc_ins_field_sync_l, field_sync_l, u8);
field!(ppc_ins_field_ds, field_ds, i16);
field!(ppc_ins_field_sh64, field_sh64, u8);
field!(ppc_ins_field_mb64, field_mb64, u8);
field!(ppc_ins_field_me64, field_me64, u8);
field!(ppc_ins_field_mtmsrd_l, field_mtmsrd_l, u8);
field!(ppc_ins_field_ps_offset, field_ps_offset, i16);
field!(ppc_ins_field_ps_i, field_ps_i, u8);
field!(ppc_ins_field_ps_ix, field_ps_ix, u8);
field!(ppc_ins_field_ps_w, field_ps_w, u8);
field!(ppc_ins_field_ps_wx, field_ps_wx, u8);
field!(ppc_ins_field_vsimm, field_vsimm, i8);
field!(ppc_ins_field_vuimm, field_vuimm, u8);
field!(ppc_ins_field_vs, field_vs, u8);
field!(ppc_ins_field_vd, field_vd, u8);
field!(ppc_ins_field_va, field_va, u8);
field!(ppc_ins_field_vb, field_vb, u8);
field!(ppc_ins_field_vc, field_vc, u8);
field!(ppc_ins_field_ds_a, field_ds_a, u8);
field!(ppc_ins_field_strm, field_strm, u8);
field!(ppc_ins_field_shb, field_shb, u8);
field!(ppc_ins_field_vds128, field_vds128, u8);
field!(ppc_ins_field_va128, field_va128, u8);
field!(ppc_ins_field_vb128, field_vb128, u8);
field!(ppc_ins_field_vc128, field_vc128, u8);
field!(ppc_ins_field_perm, field_perm, u8);
field!(ppc_ins_field_d3dtype, field_d3dtype, u8);
field!(ppc_ins_field_vmask, field_vmask, u8);
field!(ppc_ins_field_zimm, field_zimm, u8);
field!(ppc_ins_field_oe, field_oe, bool);
field!(ppc_ins_field_rc, field_rc, bool);
field!(ppc_ins_field_lk, field_lk, bool);
field!(ppc_ins_field_aa, field_aa, bool);
field!(ppc_ins_field_bp, field_bp, bool);
field!(ppc_ins_field_bnp, field_bnp, bool);
field!(ppc_ins_field_bp_nd, field_bp_nd, bool);
field!(ppc_ins_field_t, field_t, bool);
field!(ppc_ins_field_rcav, field_rcav, bool);
field!(ppc_ins_field_rc128, field_rc128, bool);

#[cfg(test)]
mod powerpc_tests {
    use super::*;
    use std::ffi::CStr;

    fn text(bytes: &[u8]) -> &str {
        CStr::from_bytes_until_nul(bytes).unwrap().to_str().unwrap()
    }

    #[test]
    fn readme_operands_and_display() {
        let ins = ppc_ins_new_with_extensions(0x38a00000, Extensions::none().bitmask());
        assert_eq!(ins.opcode, powerpc::Opcode::Addi as u16);
        assert_eq!(ins.extensions, 0);

        let basic = ppc_ins_basic(ins);
        assert_eq!(text(&basic.text), "addi r5, r0, 0x0");
        assert_eq!(basic.args.count, 3);
        assert_eq!(basic.args.items[0].kind, PpcArgumentKind::Gpr);
        assert_eq!(unsafe { basic.args.items[0].value.gpr }, 5);
        assert_eq!(basic.args.items[1].kind, PpcArgumentKind::Gpr);
        assert_eq!(unsafe { basic.args.items[1].value.gpr }, 0);
        assert_eq!(basic.args.items[2].kind, PpcArgumentKind::Simm);
        assert_eq!(unsafe { basic.args.items[2].value.simm }, 0);

        let simplified = ppc_ins_simplified(ins);
        assert_eq!(text(&simplified.text), "li r5, 0x0");
        assert_eq!(simplified.args.count, 2);
    }

    #[test]
    fn branch_fields_and_target() {
        let branch = ppc_ins_new_with_extensions(0x48000001, 0);
        assert!(ppc_ins_is_branch(branch));
        assert!(ppc_ins_is_direct_branch(branch));
        assert!(ppc_ins_is_unconditional_branch(branch));
        assert!(ppc_ins_field_lk(branch));
        assert!(!ppc_ins_field_aa(branch));
        let offset = ppc_ins_branch_offset(branch);
        assert_eq!(offset.valid, 1);
        assert_eq!(offset.offset, 0);
        let dest = ppc_ins_branch_dest(branch, 0x1000);
        assert_eq!(dest.valid, 1);
        assert_eq!(dest.address, 0x1000);
    }

    #[test]
    fn decoded_instruction_keeps_its_extensions() {
        let code = 0x1243388f;
        let no_extensions = ppc_ins_new_with_extensions(code, 0);
        let xenon = ppc_ins_new(code);
        assert_eq!(no_extensions.opcode, powerpc::Opcode::Illegal as u16);
        assert_eq!(xenon.opcode, powerpc::Opcode::Lvewx128 as u16);
        assert_eq!(text(&ppc_ins_simplified(xenon).text), "lvewx128 v114, r3, r7");
    }
}
