use powerpc::{Argument, Arguments, Extensions, Ins, ParsedIns};

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
#[derive(Clone, Copy, Default)]
pub struct PpcParsedIns {
    pub mnemonic: [u8; 32],
    pub args: PpcArguments,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct PpcIns {
    pub code: u32,
    pub opcode: u16,
}

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct PpcBranchDest {
    pub valid: u8,
    pub address: u32,
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

fn name(text: &str) -> [u8; 32] {
    let mut result = [0; 32];
    let bytes = text.as_bytes();
    let len = bytes.len().min(result.len() - 1);
    result[..len].copy_from_slice(&bytes[..len]);
    result
}

fn parsed(ins: ParsedIns) -> PpcParsedIns {
    PpcParsedIns {
        mnemonic: name(ins.mnemonic),
        args: arguments(ins.args),
    }
}

fn decode(ins: PpcIns) -> Ins {
    Ins::new(ins.code, Extensions::xenon())
}

#[no_mangle]
pub extern "C" fn ppc_ins_new(code: u32) -> PpcIns {
    let ins = Ins::new(code, Extensions::xenon());
    PpcIns {
        code,
        opcode: ins.op as u16,
    }
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
pub extern "C" fn ppc_ins_branch_dest(ins: PpcIns, address: u32) -> PpcBranchDest {
    match decode(ins).branch_dest(address) {
        Some(address) => PpcBranchDest { valid: 1, address },
        None => PpcBranchDest::default(),
    }
}
