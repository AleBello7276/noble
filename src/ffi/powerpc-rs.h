#pragma once

#include "powerpc_ffi.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace codec {

using Opcode = PpcOpcode;

// retain the same extension set used when the instruction was decoded
class Extensions {
public:
    constexpr Extensions() noexcept = default;
    explicit constexpr Extensions(std::uint32_t bits) noexcept : bits_(bits) {}

    static constexpr Extensions none() noexcept { return Extensions{}; }
    static Extensions xenon() noexcept { return Extensions(ppc_extensions_xenon()); }
    static Extensions gekko_broadway() noexcept { return Extensions(ppc_extensions_gekko_broadway()); }
    static constexpr Extensions from_bitmask(std::uint32_t bits) noexcept { return Extensions(bits); }
    constexpr std::uint32_t bitmask() const noexcept { return bits_; }

private:
    std::uint32_t bits_ = 0;
};

#define PPC_OPERAND_TYPE(name, type)                                                                         \
    struct name {                                                                                            \
        type value;                                                                                          \
        bool operator==(const name&) const = default;                                                        \
    };
PPC_OPERAND_TYPE(GPR, std::uint8_t)
PPC_OPERAND_TYPE(FPR, std::uint8_t)
PPC_OPERAND_TYPE(SR, std::uint8_t)
PPC_OPERAND_TYPE(SPR, std::uint16_t)
PPC_OPERAND_TYPE(CRField, std::uint8_t)
PPC_OPERAND_TYPE(CRBit, std::uint8_t)
PPC_OPERAND_TYPE(GQR, std::uint8_t)
PPC_OPERAND_TYPE(Uimm, std::uint16_t)
PPC_OPERAND_TYPE(Simm, std::int16_t)
PPC_OPERAND_TYPE(Offset, std::int16_t)
PPC_OPERAND_TYPE(BranchDest, std::int32_t)
PPC_OPERAND_TYPE(OpaqueU, std::uint16_t)
PPC_OPERAND_TYPE(VR, std::uint8_t)
#undef PPC_OPERAND_TYPE

using ArgumentValue = std::variant<std::monostate, GPR, FPR, SR, SPR, CRField, CRBit, GQR, Uimm, Simm, Offset,
                                   BranchDest, OpaqueU, VR>;

struct Argument {
    ArgumentValue value;

    Argument() = default;
    template <typename T>
    Argument(T operand) : value(operand) {}

    template <typename T>
    bool is() const noexcept {
        return std::holds_alternative<T>(value);
    }

    template <typename T>
    const T* get_if() const noexcept {
        return std::get_if<T>(&value);
    }

    bool operator==(const Argument&) const = default;
};

struct Arguments {
    std::array<Argument, 5> items{};
    std::uint8_t count = 0;

    const Argument& operator[](std::size_t index) const noexcept { return items[index]; }
    std::size_t size() const noexcept { return count; }
    auto begin() const noexcept { return items.begin(); }
    auto end() const noexcept { return items.begin() + count; }
};

struct ParsedIns {
    std::string mnemonic;
    Arguments args;
    std::string text;

    std::string_view to_string() const noexcept { return text; }
};

inline Argument from_ffi(PpcArgument arg) {
    switch (arg.kind) {
    case PPC_GPR:
        return GPR{arg.value.gpr};
    case PPC_FPR:
        return FPR{arg.value.fpr};
    case PPC_SR:
        return SR{arg.value.sr};
    case PPC_SPR:
        return SPR{arg.value.spr};
    case PPC_CR_FIELD:
        return CRField{arg.value.cr_field};
    case PPC_CR_BIT:
        return CRBit{arg.value.cr_bit};
    case PPC_GQR:
        return GQR{arg.value.gqr};
    case PPC_UIMM:
        return Uimm{arg.value.uimm};
    case PPC_SIMM:
        return Simm{arg.value.simm};
    case PPC_OFFSET:
        return Offset{arg.value.offset};
    case PPC_BRANCH_DEST:
        return BranchDest{arg.value.branch_dest};
    case PPC_OPAQUE_U:
        return OpaqueU{arg.value.opaque_u};
    case PPC_VR:
        return VR{arg.value.vr};
    case PPC_NONE:
        return {};
    }
    return {};
}

inline Arguments from_ffi(PpcArguments raw) {
    Arguments result;
    result.count
        = raw.count <= result.items.size() ? raw.count : static_cast<std::uint8_t>(result.items.size());
    for (std::size_t i = 0; i < result.items.size(); ++i)
        result.items[i] = from_ffi(raw.items[i]);
    return result;
}

inline ParsedIns from_ffi(PpcParsedIns raw) {
    return {raw.mnemonic, from_ffi(raw.args), raw.text};
}

struct Ins {
    std::uint32_t code;
    Opcode op;
    Extensions extensions;

    explicit Ins(std::uint32_t encoded, Extensions enabled = Extensions::xenon()) noexcept
        : Ins(ppc_ins_new_with_extensions(encoded, enabled.bitmask())) {}
    explicit Ins(PpcIns raw) noexcept
        : code(raw.code), op(raw.opcode), extensions(Extensions::from_bitmask(raw.extensions)) {}

    PpcIns raw() const noexcept { return {code, op, extensions.bitmask()}; }
    ParsedIns basic() const { return from_ffi(ppc_ins_basic(raw())); }
    ParsedIns simplified() const { return from_ffi(ppc_ins_simplified(raw())); }
    Arguments defs() const { return from_ffi(ppc_ins_defs(raw())); }
    Arguments uses() const { return from_ffi(ppc_ins_uses(raw())); }

    bool is_branch() const noexcept { return ppc_ins_is_branch(raw()); }
    bool is_direct_branch() const noexcept { return ppc_ins_is_direct_branch(raw()); }
    bool is_unconditional_branch() const noexcept { return ppc_ins_is_unconditional_branch(raw()); }
    bool is_conditional_branch() const noexcept { return ppc_ins_is_conditional_branch(raw()); }
    bool is_blr() const noexcept { return ppc_ins_is_blr(raw()); }

    std::optional<std::int32_t> branch_offset() const noexcept {
        auto result = ppc_ins_branch_offset(raw());
        return result.valid ? std::optional<std::int32_t>(result.offset) : std::nullopt;
    }
    std::optional<std::uint32_t> branch_dest(std::uint32_t address) const noexcept {
        auto result = ppc_ins_branch_dest(raw(), address);
        return result.valid ? std::optional<std::uint32_t>(result.address) : std::nullopt;
    }

    // expose the raw fields provided by powerpc-rs
    std::int16_t field_simm() const noexcept { return ppc_ins_field_simm(raw()); }
    std::uint16_t field_uimm() const noexcept { return ppc_ins_field_uimm(raw()); }
    std::int16_t field_offset() const noexcept { return ppc_ins_field_offset(raw()); }
    std::uint8_t field_bo() const noexcept { return ppc_ins_field_bo(raw()); }
    std::uint8_t field_bi() const noexcept { return ppc_ins_field_bi(raw()); }
    std::int16_t field_bd() const noexcept { return ppc_ins_field_bd(raw()); }
    std::int32_t field_li() const noexcept { return ppc_ins_field_li(raw()); }
    std::uint8_t field_sh() const noexcept { return ppc_ins_field_sh(raw()); }
    std::uint8_t field_mb() const noexcept { return ppc_ins_field_mb(raw()); }
    std::uint8_t field_me() const noexcept { return ppc_ins_field_me(raw()); }
    std::uint8_t field_rs() const noexcept { return ppc_ins_field_rs(raw()); }
    std::uint8_t field_rd() const noexcept { return ppc_ins_field_rd(raw()); }
    std::uint8_t field_ra() const noexcept { return ppc_ins_field_ra(raw()); }
    std::uint8_t field_rb() const noexcept { return ppc_ins_field_rb(raw()); }
    std::uint8_t field_sr() const noexcept { return ppc_ins_field_sr(raw()); }
    std::uint16_t field_spr() const noexcept { return ppc_ins_field_spr(raw()); }
    std::uint8_t field_frs() const noexcept { return ppc_ins_field_frs(raw()); }
    std::uint8_t field_frd() const noexcept { return ppc_ins_field_frd(raw()); }
    std::uint8_t field_fra() const noexcept { return ppc_ins_field_fra(raw()); }
    std::uint8_t field_frb() const noexcept { return ppc_ins_field_frb(raw()); }
    std::uint8_t field_frc() const noexcept { return ppc_ins_field_frc(raw()); }
    std::uint8_t field_crbd() const noexcept { return ppc_ins_field_crbd(raw()); }
    std::uint8_t field_crba() const noexcept { return ppc_ins_field_crba(raw()); }
    std::uint8_t field_crbb() const noexcept { return ppc_ins_field_crbb(raw()); }
    std::uint8_t field_crfd() const noexcept { return ppc_ins_field_crfd(raw()); }
    std::uint8_t field_crfs() const noexcept { return ppc_ins_field_crfs(raw()); }
    std::uint8_t field_crm() const noexcept { return ppc_ins_field_crm(raw()); }
    std::uint8_t field_nb() const noexcept { return ppc_ins_field_nb(raw()); }
    std::uint16_t field_tbr() const noexcept { return ppc_ins_field_tbr(raw()); }
    std::uint8_t field_mtfsf_fm() const noexcept { return ppc_ins_field_mtfsf_fm(raw()); }
    std::uint8_t field_mtfsf_imm() const noexcept { return ppc_ins_field_mtfsf_imm(raw()); }
    std::uint8_t field_spr_sprg() const noexcept { return ppc_ins_field_spr_sprg(raw()); }
    std::uint8_t field_spr_bat() const noexcept { return ppc_ins_field_spr_bat(raw()); }
    std::uint8_t field_to() const noexcept { return ppc_ins_field_to(raw()); }
    std::uint8_t field_l() const noexcept { return ppc_ins_field_l(raw()); }
    std::uint8_t field_sync_l() const noexcept { return ppc_ins_field_sync_l(raw()); }
    std::int16_t field_ds() const noexcept { return ppc_ins_field_ds(raw()); }
    std::uint8_t field_sh64() const noexcept { return ppc_ins_field_sh64(raw()); }
    std::uint8_t field_mb64() const noexcept { return ppc_ins_field_mb64(raw()); }
    std::uint8_t field_me64() const noexcept { return ppc_ins_field_me64(raw()); }
    std::uint8_t field_mtmsrd_l() const noexcept { return ppc_ins_field_mtmsrd_l(raw()); }
    std::int16_t field_ps_offset() const noexcept { return ppc_ins_field_ps_offset(raw()); }
    std::uint8_t field_ps_i() const noexcept { return ppc_ins_field_ps_i(raw()); }
    std::uint8_t field_ps_ix() const noexcept { return ppc_ins_field_ps_ix(raw()); }
    std::uint8_t field_ps_w() const noexcept { return ppc_ins_field_ps_w(raw()); }
    std::uint8_t field_ps_wx() const noexcept { return ppc_ins_field_ps_wx(raw()); }
    std::int8_t field_vsimm() const noexcept { return ppc_ins_field_vsimm(raw()); }
    std::uint8_t field_vuimm() const noexcept { return ppc_ins_field_vuimm(raw()); }
    std::uint8_t field_vs() const noexcept { return ppc_ins_field_vs(raw()); }
    std::uint8_t field_vd() const noexcept { return ppc_ins_field_vd(raw()); }
    std::uint8_t field_va() const noexcept { return ppc_ins_field_va(raw()); }
    std::uint8_t field_vb() const noexcept { return ppc_ins_field_vb(raw()); }
    std::uint8_t field_vc() const noexcept { return ppc_ins_field_vc(raw()); }
    std::uint8_t field_ds_a() const noexcept { return ppc_ins_field_ds_a(raw()); }
    std::uint8_t field_strm() const noexcept { return ppc_ins_field_strm(raw()); }
    std::uint8_t field_shb() const noexcept { return ppc_ins_field_shb(raw()); }
    std::uint8_t field_vds128() const noexcept { return ppc_ins_field_vds128(raw()); }
    std::uint8_t field_va128() const noexcept { return ppc_ins_field_va128(raw()); }
    std::uint8_t field_vb128() const noexcept { return ppc_ins_field_vb128(raw()); }
    std::uint8_t field_vc128() const noexcept { return ppc_ins_field_vc128(raw()); }
    std::uint8_t field_perm() const noexcept { return ppc_ins_field_perm(raw()); }
    std::uint8_t field_d3dtype() const noexcept { return ppc_ins_field_d3dtype(raw()); }
    std::uint8_t field_vmask() const noexcept { return ppc_ins_field_vmask(raw()); }
    std::uint8_t field_zimm() const noexcept { return ppc_ins_field_zimm(raw()); }
    bool field_oe() const noexcept { return ppc_ins_field_oe(raw()); }
    bool field_rc() const noexcept { return ppc_ins_field_rc(raw()); }
    bool field_lk() const noexcept { return ppc_ins_field_lk(raw()); }
    bool field_aa() const noexcept { return ppc_ins_field_aa(raw()); }
    bool field_bp() const noexcept { return ppc_ins_field_bp(raw()); }
    bool field_bnp() const noexcept { return ppc_ins_field_bnp(raw()); }
    bool field_bp_nd() const noexcept { return ppc_ins_field_bp_nd(raw()); }
    bool field_t() const noexcept { return ppc_ins_field_t(raw()); }
    bool field_rcav() const noexcept { return ppc_ins_field_rcav(raw()); }
    bool field_rc128() const noexcept { return ppc_ins_field_rc128(raw()); }
};

}  // namespace codec
