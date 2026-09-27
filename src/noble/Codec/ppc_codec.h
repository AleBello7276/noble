#pragma once
#include <cstdint>
#include <string>
#include <array>
#include <stdexcept>
#include <format>

/*
    ------ LLVM360 PowerPC Codec ------
    PowerPC instruction Decoder/Encoder Header only implementation
*/

#define REGISTER_INSTR(name) \
    static const name g##name; 

namespace codec {


    struct PPCInstruction;

    enum PPCInstrType {
        I_Undefined,
        I_PADDING,
        I_ADDI,

        ENUM_COUNT
    };

    /*
     this is a Core Codec struct
     Instructions have fields, each field is at an offset and have width in bits
     this struct provides functionality to extract and insert data of said field into a word
    */
    template<uint32_t off, uint32_t width, bool isSigned = false>
    struct Field {
        static_assert(width > 0 && width <= 32, "WIDTH IS INCOMPATIBLE");
        static_assert(off + width <= 32, "FIELD IS OUT OF RANGE");

        static constexpr uint32_t valueMask =
            (width == 32) ? 0xFFFFFFFFu : ((1u << width) - 1u);
        static constexpr uint32_t beOff = (32 - off) - width;

        static constexpr auto extract(uint32_t w) {
            uint32_t v = (w >> beOff) & valueMask;
            // if signed field, get sign bit and extend
            if constexpr(isSigned) {
                uint32_t sign = 1u << (width - 1);
                return static_cast<int32_t>((v ^ sign) - sign);
            }
            else {
                return v;
            }
        }

        static constexpr void insert(uint32_t& w, uint32_t v) {
            w = (w & ~valueMask) | ((v & valueMask) << beOff);
        }
    };
    
    
    struct Encoder {
    private:
        uint32_t word = 0;
    public:
        
        template<typename fieldT>
        constexpr Encoder& set(uint32_t v) {
            fieldT::insert(word, v);
            return *this;
        }

        uint32_t value() const { return word; }
    };

    struct GPRReg {
        uint32_t regNum;
        GPRReg(uint32_t r) : regNum(r) {
            if(regNum > 31) {
                throw std::out_of_range("GPR register number out of range");
            }
        }
        operator uint32_t() const { return regNum; }
    };

    struct SIMMVal {
        int16_t value;
        SIMMVal(int16_t v) : value(v) {}
        operator int16_t() const { return value; }
    };
    
    using OPCD = Field<0, 6>;
    using RT = Field<6, 5>;
    using RA = Field<11, 5>;
    using SIMM = Field<16, 16, true>;

    
    static const std::string gprString(uint32_t regNum) {
        return "r" + std::to_string(regNum);
    }

    static const std::string simmString(int16_t val) {
        return std::to_string(val);
    }

    /*
    Instruction Descriptors
    */
    
    struct PPCInstruction {
        explicit PPCInstruction() = default;
        virtual ~PPCInstruction() = default;
        virtual PPCInstrType type() const { return mType; }
        virtual std::string dump(uint32_t data) const = 0;
    private:
        const PPCInstrType mType = I_Undefined;
    };
    

    struct UndefinedInst : PPCInstruction {
        UndefinedInst() : PPCInstruction() {}
        PPCInstrType type() const override { return mType; }
        std::string dump(uint32_t data) const override {
            return "undefined";
        }
    private:
        const PPCInstrType mType = I_Undefined;
    };
    REGISTER_INSTR(UndefinedInst)


    struct Addi : PPCInstruction {
        Addi() : PPCInstruction() {}
        PPCInstrType type() const override { return mType; }
        std::string dump(uint32_t data) const override {
            return "addi " + gprString(RT::extract(data)) + ", " + gprString(RA::extract(data)) + ", " + simmString(SIMM::extract(data));
        }
        static uint32_t encode(GPRReg rt, GPRReg ra, SIMMVal simm) {
            Encoder encoder;
            encoder
                .set<OPCD>(14) // ADDI opcode
                .set<RT>(rt)
                .set<RA>(ra)
                .set<SIMM>(simm);
            return encoder.value();
        }
    private:
        const PPCInstrType mType = PPCInstrType::I_ADDI;
    };
    REGISTER_INSTR(Addi)

    struct Padding : PPCInstruction {
        Padding() : PPCInstruction() {}
        PPCInstrType type() const override { return mType; }
        std::string dump(uint32_t data) const override {
            return "PADDING";
        }
        static uint32_t encode() {
            Encoder encoder; encoder.set<OPCD>(0); // PADDING
            return encoder.value();
        }
        private:
            const PPCInstrType mType = PPCInstrType::I_PADDING;
    };
    REGISTER_INSTR(Padding)



    /*
    Extended Opcode Tables
    */

    /* OPCODE 31 table*/
    static std::array<const PPCInstruction*, 2048> gOP31Table = [] {
        std::array<const PPCInstruction*, 2048> table;
        table.fill(&gUndefinedInst);

        
        return table;
        }();
    



    struct DecodedInst {
        const PPCInstruction& mInstTemplate;
        uint32_t mData;
        DecodedInst(const PPCInstruction& instTemplate, uint32_t data)
            : mInstTemplate(instTemplate), mData(data) {
        }
    };
    
    class PPCCodec {
    public:
        
        static inline DecodedInst decode(uint32_t data) {
            data = _bswap(data);

            uint32_t mainOP = OPCD::extract(data);
            switch(mainOP) {
            case 0: // Padding
                return { gPadding, data };
            case 14: // ADDI
                return { gAddi, data };

            default:
                throw std::out_of_range(std::format("PPCCodec::decode -> Unknown main opcode: 0x{:02X}", mainOP));
                return { gUndefinedInst, data };
            }

            return { gUndefinedInst, data };
        }
    };
} // codec