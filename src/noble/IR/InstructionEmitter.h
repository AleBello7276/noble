#pragma once
#include "IRGenerator.h"
#include "IRFunc.h"
#include <unordered_set>


//
// Helpers
//

#define GEN  func->m_irGen
#define BUILD  func->m_irGen->m_builder
#define i64Const(x) BUILD->getInt64(x)
#define i32Const(x) BUILD->getInt32(x)
#define i16Const(x) BUILD->getInt16(x)
#define i8Const(x) BUILD->getInt8(x)
#define i1Const(x) BUILD->getInt1(x)

#define i64_T BUILD->getInt64Ty()
#define i32_T BUILD->getInt32Ty()
#define i16_T BUILD->getInt16Ty()
#define i8_T BUILD->getInt8Ty()
#define i1_T BUILD->getInt1Ty()

#define trcTo1(x) BUILD->CreateTrunc(x, BUILD->getInt1Ty(), "trc1")
#define trcTo8(x) BUILD->CreateTrunc(x, BUILD->getInt8Ty(), "trc8")
#define trcTo16(x) BUILD->CreateTrunc(x, BUILD->getInt16Ty(), "trc16")
#define trcTo32(x) BUILD->CreateTrunc(x, BUILD->getInt32Ty(), "trc32")
#define trcTo64(x) BUILD->CreateTrunc(x, BUILD->getInt64Ty(), "trc64")

// register value access / store
#define gprVal(x) BUILD->CreateLoad(BUILD->getInt64Ty(), func->getRegister("RR", x), "rrV")
#define crVal() BUILD->CreateLoad(BUILD->getInt32Ty(), func->getRegister("CR"), "crV")
#define xerVal() BUILD->CreateLoad(BUILD->getInt32Ty(), func->getRegister("XER"), "xerV")
#define ctrVal() BUILD->CreateLoad(BUILD->getInt32Ty(), func->getRegister("CTR"), "ctrV")


// load - store
#define Load8(x)  BUILD->CreateLoad(i8_T, EA_HostPtr(func, x), "ld8")
#define Load16(x) BUILD->CreateCall(GEN->swap16, BUILD->CreateLoad(i16_T, EA_HostPtr(func, x), "ld16"), "sp16")
#define Load32(x) BUILD->CreateCall(GEN->swap32, BUILD->CreateLoad(i32_T, EA_HostPtr(func, x), "ld32"), "sp32")
#define Load64(x) BUILD->CreateCall(GEN->swap64, BUILD->CreateLoad(i64_T, EA_HostPtr(func, x), "ld64"), "sp64")

#define Store8(x, y)  BUILD->CreateStore(trcTo8(x), EA_HostPtr(func, y));
#define Store16(x, y) BUILD->CreateStore(BUILD->CreateCall(GEN->swap16, trcTo16(x), "sp16"), EA_HostPtr(func, y));
#define Store32(x, y) BUILD->CreateStore(BUILD->CreateCall(GEN->swap32, trcTo32(x), "sp32"), EA_HostPtr(func, y));
#define Store64(x, y) BUILD->CreateStore(BUILD->CreateCall(GEN->swap64, trcTo64(x), "sp64"), EA_HostPtr(func, y));


// ext
#define sExt64(x) BUILD->CreateSExt(x, BUILD->getInt64Ty(), "sEx64")
#define sExt32(x) BUILD->CreateSExt(x, BUILD->getInt32Ty(), "sEx32")

#define zExt64(x) BUILD->CreateZExt(x, BUILD->getInt64Ty(), "zEx64")
#define zExt32(x) BUILD->CreateZExt(x, BUILD->getInt32Ty(), "zEx32")

#define sign64(x)  llvm::ConstantInt::getSigned(i64_T, x)
#define sign32(x)  llvm::ConstantInt::getSigned(i32_T, x)
#define sign16(x)  llvm::ConstantInt::getSigned(i16_T, x)


inline void StoreCA(IRFunc* func, llvm::Value* ca)
{
    BUILD->CreateStore(BUILD->CreateOr(xerVal(), BUILD->CreateShl(zExt32(ca), 2, "shl"), "or"), func->getRegister("XER"));
}

inline llvm::Value* getCA(IRFunc* func)
{
	return BUILD->CreateAnd(BUILD->CreateLShr(xerVal(), 2, "lshr"), 0b11, "and");
}

// https://github.com/xenia-canary/xenia-canary/blob/canary_experimental/src/xenia/cpu/ppc/ppc_decode_data.h#L29
static inline uint64_t XEMASK(uint32_t mstart, uint32_t mstop) {
    // if mstart ≤ mstop then
    //   mask[mstart:mstop] = ones
    //   mask[all other bits] = zeros
    // else
    //   mask[mstart:63] = ones
    //   mask[0:mstop] = ones
    //   mask[all other bits] = zeros
    mstart &= 0x3F;
    mstop &= 0x3F;
    uint64_t value =
        (UINT64_MAX >> mstart) ^ ((mstop >= 63) ? 0 : UINT64_MAX >> (mstop + 1));
    return mstart <= mstop ? value : ~value;
}

inline uint32_t signExtend(uint32_t value, int size)
{
    if (value & (1 << (size - 1))) {
        return value | (~0 << size); 
    }
    return value; 
}

inline bool isBoBit(uint32_t value, uint32_t idx) {
    return (value >> idx) & 0b1;
}

inline llvm::Value* getBOOperation(IRFunc* func, Instruction instr, llvm::Value* bi)
{
    llvm::Value* should_branch{};
    // NOTE, remember to cast "bools" with int1Ty, cause if i do CreateNot with an 32 bit value it will mess up the cmp result

    // 0000y Decrement the CTR, then branch if the decremented CTR[M–63] is not 0 and the condition is FALSE.
    if (instr.ops[0] == 0)
    {
        BUILD->CreateStore(BUILD->CreateSub(ctrVal(), i32Const(1), "sub"), func->getRegister("CTR"));
        llvm::Value* isCTRnz = BUILD->CreateICmpNE(ctrVal(), i32Const(0), "ctrnz");
        llvm::Value* isCDFalse = BUILD->CreateAnd(BUILD->CreateNot(bi, "not"), i1Const(1), "shBr");
        should_branch = BUILD->CreateAnd(isCTRnz, isCDFalse, "and");
        return should_branch;
    }
    // 001zy
    if (isBoBit(instr.ops[0], 2) &&
        !isBoBit(instr.ops[0], 3) && !isBoBit(instr.ops[0], 4))
    {
		// TODO: optmize this
        should_branch = BUILD->CreateAnd(BUILD->CreateNot(bi, "not"), i1Const(1), "shBr");
        return should_branch;
    }
    // 0b011zy (Branch if condition is TRUE)
    if (isBoBit(instr.ops[0], 2) && isBoBit(instr.ops[0], 3) && !isBoBit(instr.ops[0], 4))
    {
        should_branch = BUILD->CreateAnd(bi, i1Const(1), "shBr");
        return should_branch;
    }
    // 0b1z00y
    if (!isBoBit(instr.ops[0], 1) && !isBoBit(instr.ops[0], 2) && isBoBit(instr.ops[0], 4))
    {
        BUILD->CreateStore(BUILD->CreateSub(ctrVal(), i32Const(1), "sub"), func->getRegister("CTR"));
        llvm::Value* isCTRnz = BUILD->CreateICmpNE(ctrVal(), i32Const(0), "ctrnz");
        should_branch = BUILD->CreateAnd(isCTRnz, i1Const(1), "shBr");
        return should_branch;
    }
    // 1z01y Decrement the CTR, then branch if the decremented CTR[M–63] = 0
    if (isBoBit(instr.ops[0], 1) && !isBoBit(instr.ops[0], 2) && isBoBit(instr.ops[0], 4))
    {
        BUILD->CreateStore(BUILD->CreateSub(ctrVal(), i32Const(1), "sub"), func->getRegister("CTR"));
        llvm::Value* isCTRz = BUILD->CreateICmpEQ(ctrVal(), i32Const(0), "ctrnz");
        should_branch = isCTRz;
        return should_branch;
    }

    printf("un-implemented condition\n");
    DebugBreak();
    return nullptr;
}

// TODO refactor
inline void setCRField(IRFunc* func, uint32_t index, llvm::Value* field)
{
    int bitOffset = index * 4;
    llvm::Value* shiftedMask = BUILD->CreateShl(i32Const(0b1111), bitOffset, "shMsk");

    // clear the already stored bits
    llvm::Value* invertedMask = BUILD->CreateNot(shiftedMask, "inMsk");
    llvm::Value* clearedCR = BUILD->CreateAnd(crVal(), invertedMask, "zCR");
    llvm::Value* fieldExtended = zExt32(field); // Cast 'field' to i32 before shifting

    // update bits
    llvm::Value* shiftedFieldValue = BUILD->CreateShl(fieldExtended, bitOffset, "shVal");
    llvm::Value* updatedCR = BUILD->CreateOr(clearedCR, shiftedFieldValue, "uCR");

    BUILD->CreateStore(updatedCR, func->getRegister("CR"));
}

inline void UpdateCR_CmpZero(IRFunc* func, Instruction instr, const char* name, llvm::Value* val)
{
    // RC
    if (strcmp(instr.opcName.c_str(), name) == 0)
    {
        llvm::Value* LT = zExt32(BUILD->CreateICmpSLT(val, i64Const(0), "lt"));
        llvm::Value* GT = zExt32(BUILD->CreateICmpSGT(val, i64Const(0), "gt"));
        llvm::Value* EQ = zExt32(BUILD->CreateICmpEQ(val, i64Const(0), "eq"));
        // TODO
        llvm::Value* SO_bit = i32Const(0);
        llvm::Value* field = zExt32(BUILD->CreateOr(BUILD->CreateOr(BUILD->CreateOr(LT, BUILD->CreateShl(GT, 1, "sh"), "or"), BUILD->CreateShl(EQ, 2, "sh"), "or"), BUILD->CreateShl(SO_bit, 3, "sh"), "or"));

        setCRField(func, 0, field);
    }
}

inline void UpdateCR_CmpValue(IRFunc* func, Instruction instr, llvm::Value* v1, llvm::Value* v2, uint32_t field)
{
    llvm::Value* LT = zExt32(BUILD->CreateICmpSLT(v1, v2, "lt"));
    llvm::Value* GT = zExt32(BUILD->CreateICmpSGT(v1, v2, "gt"));
    llvm::Value* EQ = zExt32(BUILD->CreateICmpEQ(v1, v2, "eq"));
    // TODO
    llvm::Value* SO_bit = i32Const(0);
    llvm::Value* res = zExt32(BUILD->CreateOr(BUILD->CreateOr(BUILD->CreateOr(LT, BUILD->CreateShl(GT, 1, "sh"), "or"), BUILD->CreateShl(EQ, 2, "sh"), "or"), BUILD->CreateShl(SO_bit, 3, "sh"), "or"));
    setCRField(func, field, res);
}

inline llvm::Value* extractCRBit(IRFunc* func, uint32_t BI) {
    // create mask at bit pos
    llvm::Value* mask = i32Const(1);
    llvm::Value* shiftAmount = i32Const(BI);
    llvm::Value* shiftedMask = BUILD->CreateShl(mask, shiftAmount, "shm");

    // mask the bit
    llvm::Value* isolatedBit = BUILD->CreateAnd(crVal(), shiftedMask, "ib");
    return BUILD->CreateIntCast(BUILD->CreateLShr(isolatedBit, shiftAmount, "bi"), BUILD->getInt1Ty(), false,"cast1");
}

inline llvm::Value* AddCarried(IRFunc* func, llvm::Value* v1, llvm::Value* v2)
{
    return BUILD->CreateICmpUGT(trcTo32(v2), BUILD->CreateNot(trcTo32(v1), "not"), "ugt");
}

inline llvm::Value* SubCarried(IRFunc* func, llvm::Value* v1, llvm::Value* v2)
{
    return BUILD->CreateICmpSLE(v1, v2, "sle");
}


inline llvm::Value* guestToHost(IRFunc* func, llvm::Value* guest)
{
    return BUILD->CreateAdd(guest, BUILD->CreateLoad(func->m_irGen->module_base->getValueType(), func->m_irGen->module_base, "m_bV"), "fEa");
}

inline void updateRA_EA(IRFunc* func, Instruction instr, llvm::Value* eaVal)
{
    BUILD->CreateStore(eaVal, func->getRegister("RR", instr.ops[2])); // update rA
}

inline llvm::Value* EA_HostPtr(IRFunc* func, llvm::Value* guestEa)
{
    return BUILD->CreateIntToPtr(guestToHost(func, guestEa), i64_T->getPointerTo(), "addrPtr");
}

inline llvm::Value* getEA_D(IRFunc* func, uint32_t displ, uint32_t gpr)
{
    llvm::Value* b = gpr == 0 ? static_cast<llvm::Value*>(i64Const(0)) : static_cast<llvm::Value*>(gprVal(gpr));
    return zExt64(trcTo32(BUILD->CreateAdd(b, i64Const((int64_t)((int16_t)(displ))), "ea")));
}

inline llvm::Value* getEA_DWORD_D(IRFunc* func, uint32_t displ, uint32_t gpr)
{
    llvm::Value* regValue = gprVal(gpr);
    return zExt64(trcTo32(BUILD->CreateAdd(regValue, i64Const((int64_t)((int16_t)(displ << 2))), "ea")));
}

inline llvm::Value* getEA_R(IRFunc* func, uint32_t gpr1, uint32_t gpr2)
{
    llvm::Value* b = gpr1 == 0 ? static_cast<llvm::Value*>(i64Const(0)) : static_cast<llvm::Value*>(gprVal(gpr1));
    return zExt64(trcTo32(BUILD->CreateAdd(b, gprVal(gpr2), "ea")));
}


//
// INSTRUCTIONS Emitters
//

inline void nop_e(Instruction instr, IRFunc* func)
{
	// best instruction ever

    // lil hack
    if (instr.address == func->end_address && func->m_irGen->isIRFuncinMap(instr.address + 4))
    {
        BUILD->CreateRetVoid();
    }
    return;
}

inline void dcbt_e(Instruction instr, IRFunc* func)
{
    // no-op
    return;
}

inline void dcbtst_e(Instruction instr, IRFunc* func)
{
    // no-op
    return;
}


inline void twi_e(Instruction instr, IRFunc* func)
{
    // temp stub
    DebugBreak();
    return;
}

inline void tdi_e(Instruction instr, IRFunc* func)
{
    // temp stub
    DebugBreak();
    return;
}


inline void mfspr_e(Instruction instr, IRFunc* func)
{
    auto lrValue = BUILD->CreateLoad(BUILD->getInt64Ty(), func->getSPR(instr.ops[1]), "load_spr");
    BUILD->CreateStore(lrValue, func->getRegister("RR", instr.ops[0]));
}

inline void mtspr_e(Instruction instr, IRFunc* func)
{
    auto rrValue = trcTo32(gprVal(instr.ops[1]));
    BUILD->CreateStore(rrValue, func->getSPR(instr.ops[0]));
}

inline void mfcr_e(Instruction instr, IRFunc* func)
{
    BUILD->CreateStore(crVal(), func->getRegister("RR", instr.ops[0]));
}

inline void bl_e(Instruction instr, IRFunc* func)
{
    uint32_t target = instr.address + signExtend(instr.ops[0], 24);
	IRFunc* targetFunc = func->m_irGen->getCreateFuncInMap(target);
    func->m_irGen->initFuncBody(targetFunc);

    // outdated:
    // 
    // update link register with the llvm return address of the next ir instruction
    // this is an interesting one, since i really don't have a good way to "store the instr.address + 4" in LR and make it work,
    // here is what i do, i create a new basic block for the address of the next instruction (so instr.address + 4 bytes) and store it
    // in LR, so when LR is used to return, it branch to the basic block so the next instruction
    // i think there is a better way to handle this but.. it should work fine for now :}
    // llvm::BlockAddress* lr_BB = func->getCreateBBinMap(instr.address + 4); fix it


    auto argIter = func->m_irFunc->arg_begin();
    llvm::Argument* arg1 = &*argIter;
    llvm::Argument* arg2 = &*(++argIter);

    BUILD->CreateStore(i32Const(instr.address + 4), func->getRegister("LR"));
	BUILD->CreateCall(targetFunc->m_irFunc, {arg1, i32Const(instr.address + 4)});


    uint32_t lrAddr = instr.address + 4;
    Instruction lrInstr = func->m_irGen->instrsList.at(lrAddr);

    while (strcmp(lrInstr.opcName.c_str(), "nop") == 0)
    {
        lrAddr += 4;
        lrInstr = func->m_irGen->instrsList.at(lrAddr);
    }

    // check if the lr target is a function, if yes, restore execution flow to that
    if (func->m_irGen->isIRFuncinMap(lrAddr))
    {
        IRFunc* lrFunc = func->m_irGen->getCreateFuncInMap(lrAddr);
        func->m_irGen->initFuncBody(lrFunc);
        BUILD->CreateCall(lrFunc->m_irFunc, { arg1, i32Const(lrAddr)});
        BUILD->CreateRetVoid();
    }
}

inline void b_e(Instruction instr, IRFunc* func)
{
    uint32_t target = instr.address + signExtend(instr.ops[0], 24);
    // tail call
    if(func->m_irGen->isIRFuncinMap(target))
    {
        auto argIter = func->m_irFunc->arg_begin();
        llvm::Argument* arg1 = &*argIter;
        llvm::Argument* arg2 = &*(++argIter);

        IRFunc* tailCall = func->m_irGen->getCreateFuncInMap(target);
		if (tailCall->m_irFunc == nullptr) func->m_irGen->initFuncBody(tailCall);
        BUILD->CreateCall(tailCall->m_irFunc, { arg1, arg2 });
        BUILD->CreateRetVoid();
        return;
    }

    llvm::BasicBlock* target_BB = func->getCreateBBinMap(target);
    BUILD->CreateBr(target_BB);
}

inline void bclr_e(Instruction instr, IRFunc* func)
{
	BUILD->CreateRetVoid();
}

inline void bcctrl_e(Instruction instr, IRFunc* func)
{
    auto argIter = func->m_irFunc->arg_begin();
    llvm::Argument* arg1 = &*argIter;
    llvm::Argument* arg2 = &*(++argIter);

    BUILD->CreateCall(func->m_irGen->bcctrlFunc, { arg1, i32Const(instr.address + 4) });
}

inline void bcctr_e(Instruction instr, IRFunc* func)
{
    //if (func->has_jumpTable)
    //{
    //    for (JumpTable* table : func->jumpTables)
    //    {
    //        if (instr.address >= table->start_Address && instr.address <= table->end_Address)
    //        {
    //            llvm::SwitchInst* Switch = BUILD->CreateSwitch(ctrVal(), func->getCreateBBinMap(table->targets[0]), table->targets.size());
    //            std::unordered_set<uint32_t> processedValues; // do not allow duplicates
    //            for (uint32_t target : table->targets)
    //            {
    //                if (processedValues.find(target) == processedValues.end())
    //                {
    //                    Switch->addCase(i32Const(target), func->getCreateBBinMap(target));
    //                    processedValues.insert(target);
    //                }
    //            }
    //            return;
    //        }
    //    }
    //}

    DebugBreak();
    auto argIter = func->m_irFunc->arg_begin();
    llvm::Argument* arg1 = &*argIter;
    llvm::Argument* arg2 = &*(++argIter);
    BUILD->CreateCall(func->m_irGen->bcctrlFunc, { arg1, i32Const(instr.address + 4) });

    // here i also make a return, because this is the form that do not save LR
    // so when the runtime handler return it will return to the next address of this
    // instruction, but we actually want to return to the last time lr was "stored"
    BUILD->CreateRetVoid();
    return;
}

inline void stfd_e(Instruction instr, IRFunc* func)
{
    auto frValue = BUILD->CreateLoad(BUILD->getDoubleTy(), func->getRegister("FR", instr.ops[0]), "load_fr");
    BUILD->CreateStore(frValue, EA_HostPtr(func, getEA_D(func, instr.ops[1], instr.ops[2]))); // address needs to be a pointer (pointer to an address in memory)
}

inline void addi_e(Instruction instr, IRFunc* func)
{
    llvm::Value* im = sExt64(BUILD->getInt16(instr.ops[2]));
    llvm::Value* rrValue;
    if (instr.ops[1] != 0)
    {
		rrValue = gprVal(instr.ops[1]);
	}
	else
	{
		rrValue = i64Const(0);
	}
    llvm::Value* val = instr.ops[1] ? BUILD->CreateAdd(rrValue, im, "val") : im;
    BUILD->CreateStore(val, func->getRegister("RR", instr.ops[0]));
}

inline void addic_e(Instruction instr, IRFunc* func)
{
    llvm::Value* im = sExt64(BUILD->getInt16(instr.ops[2]));
    llvm::Value* rrValue = gprVal(instr.ops[1]);
    
    llvm::Value* val = BUILD->CreateAdd(rrValue, im, "val");
    BUILD->CreateStore(val, func->getRegister("RR", instr.ops[0]));


	StoreCA(func, AddCarried(func, rrValue, im));
    UpdateCR_CmpZero(func, instr, "addicRC", val);
}



// add immediate shifted
// if rA = 0 then it will use value 0 and not the content of rA (because lis use 0)
inline void addis_e(Instruction instr, IRFunc* func)
{
    int16_t imm = static_cast<int16_t>(instr.ops[2]);
    int64_t shiftedImm = static_cast<int64_t>(imm) << 16;

    llvm::Value* shift = i64Const(shiftedImm);
    llvm::Value* rrValue;
    if (instr.ops[1] != 0)
    {
        rrValue = gprVal(instr.ops[1]);
    }
    else
    {
        rrValue = i64Const(0);
    }
	llvm::Value* val = instr.ops[1] ? BUILD->CreateAdd(rrValue, shift, "val") : shift;
    BUILD->CreateStore(val, func->getRegister("RR", instr.ops[0]));
}

inline void adde_e(Instruction instr, IRFunc* func)
{
    llvm::Value* ab = BUILD->CreateAdd(gprVal(instr.ops[1]), gprVal(instr.ops[2]), "val");
	llvm::Value* abXer = BUILD->CreateAdd(ab, zExt64(getCA(func)), "valXer");
    BUILD->CreateStore(abXer, func->getRegister("RR", instr.ops[0]));
}

inline void addze_e(Instruction instr, IRFunc* func)
{
    llvm::Value* ab = BUILD->CreateAdd(gprVal(instr.ops[1]), zExt64(getCA(func)), "val");
    BUILD->CreateStore(ab, func->getRegister("RR", instr.ops[0]));

    // XER CA and RC
    StoreCA(func, AddCarried(func, gprVal(instr.ops[1]), getCA(func)));
    UpdateCR_CmpZero(func, instr, "addzeRC", ab);
}

inline void add_e(Instruction instr, IRFunc* func)
{
    llvm::Value* val = BUILD->CreateAdd(gprVal(instr.ops[1]), gprVal(instr.ops[2]), "val");
    BUILD->CreateStore(val, func->getRegister("RR", instr.ops[0]));
}

inline void bcx_e(Instruction instr, IRFunc* func)
{
    // first check how to manage the branch condition
    // if "should_branch" == True then
    llvm::Value* bi = BUILD->CreateTrunc(extractCRBit(func, instr.ops[1]), BUILD->getInt1Ty(), "tr");
    llvm::Value* should_branch = getBOOperation(func, instr, bi);

    
    // compute condition BBs
    llvm::BasicBlock* b_true = func->getCreateBBinMap(instr.address + (int16_t)(instr.ops[2] << 2));
    llvm::BasicBlock* b_false = func->getCreateBBinMap(instr.address + 4);

    BUILD->CreateCondBr(should_branch, b_true, b_false);
}

inline void extsw_e(Instruction instr, IRFunc* func)
{
    llvm::Value* val = sExt64(trcTo32(gprVal(instr.ops[1])));
    BUILD->CreateStore(val, func->getRegister("RR", instr.ops[0]));
    UpdateCR_CmpZero(func, instr, "extswRC", val);
}

inline void extsh_e(Instruction instr, IRFunc* func)
{
    llvm::Value* val = sExt64(trcTo16(gprVal(instr.ops[1])));
    BUILD->CreateStore(val, func->getRegister("RR", instr.ops[0]));
    UpdateCR_CmpZero(func, instr, "extshRC", val);
}

inline void extsb_e(Instruction instr, IRFunc* func)
{
    llvm::Value* val = sExt64(trcTo8(gprVal(instr.ops[1])));
	BUILD->CreateStore(val, func->getRegister("RR", instr.ops[0]));
    UpdateCR_CmpZero(func, instr, "extsbRC", val);
}

inline void cmpli_e(Instruction instr, IRFunc* func)
{
    llvm::Value* a;
    if(instr.ops[1] = 0)
    {
        llvm::Value* low32Bits = trcTo32(gprVal(instr.ops[2]));
        a = zExt64(low32Bits);
    } 
    else
    {
        a = gprVal(instr.ops[2]);
    }

    UpdateCR_CmpValue(func, instr, a, i64Const(instr.ops[3]), instr.ops[0]);
}


inline void cmpl_e(Instruction instr, IRFunc* func)
{
    llvm::Value* a;
    llvm::Value* b;
    if (instr.ops[1] == 0)
    {
        llvm::Value* low32Bits = trcTo32(gprVal(instr.ops[2]));
        a = zExt64(low32Bits);
        llvm::Value* low32Bitsb = trcTo32(gprVal(instr.ops[3]));
        b = zExt64(low32Bitsb);
    }
    else
    {
        a = gprVal(instr.ops[2]);
        b = gprVal(instr.ops[3]);
    }

    UpdateCR_CmpValue(func, instr, a, b, instr.ops[0]);
}


inline void cmpw_e(Instruction instr, IRFunc* func)
{
    llvm::Value* rA = gprVal(instr.ops[2]);
    llvm::Value* rB = gprVal(instr.ops[3]);
    if(instr.ops[1] != 1)
    {
        rA = sExt64(trcTo32(rA));
        rB = sExt64(trcTo32(rB));
    }
    // update CR
    UpdateCR_CmpValue(func, instr, rA, rB, instr.ops[0]);
}

inline void cmpi_e(Instruction instr, IRFunc* func)
{
    llvm::Value* rA = gprVal(instr.ops[2]);
    llvm::Value* imm = sExt64(sign16(instr.ops[3]));
    if (instr.ops[1] == 0)
    {
        rA = sExt64(trcTo32(rA));
        imm = sExt64(imm);
    }
    UpdateCR_CmpValue(func, instr, rA, imm, instr.ops[0]);
}


//
// LOAD
//

// Load Word

inline void lwa_e(Instruction instr, IRFunc* func)
{
    BUILD->CreateStore(sExt64(Load32(getEA_D(func, instr.ops[1], instr.ops[2]))), func->getRegister("RR", instr.ops[0]));
}
inline void lwz_e(Instruction instr, IRFunc* func)
{
    BUILD->CreateStore(zExt64(Load32(getEA_D(func, instr.ops[1], instr.ops[2]))), func->getRegister("RR", instr.ops[0]));
}
inline void lwzu_e(Instruction instr, IRFunc* func)
{
    llvm::Value* ea = getEA_D(func, instr.ops[1], instr.ops[2]);
    BUILD->CreateStore(zExt64(Load32(ea)), func->getRegister("RR", instr.ops[0]));
    updateRA_EA(func, instr, ea);
}
inline void lwzx_e(Instruction instr, IRFunc* func)
{
    BUILD->CreateStore(zExt64(Load32(getEA_R(func, instr.ops[1], instr.ops[2]))), func->getRegister("RR", instr.ops[0]));
}

// Load Half-Word

inline void lhz_e(Instruction instr, IRFunc* func)
{
    BUILD->CreateStore(zExt64(Load16(getEA_D(func, instr.ops[1], instr.ops[2]))), func->getRegister("RR", instr.ops[0]));
}
inline void lhzu_e(Instruction instr, IRFunc* func)
{
    llvm::Value* ea = getEA_D(func, instr.ops[1], instr.ops[2]);
    BUILD->CreateStore(zExt64(Load16(ea)), func->getRegister("RR", instr.ops[0]));
    updateRA_EA(func, instr, ea);
}
inline void lha_e(Instruction instr, IRFunc* func)
{
    BUILD->CreateStore(sExt64(Load16(getEA_D(func, instr.ops[1], instr.ops[2]))), func->getRegister("RR", instr.ops[0]));
}
inline void lhzx_e(Instruction instr, IRFunc* func)
{
    BUILD->CreateStore(zExt64(Load16(getEA_R(func, instr.ops[1], instr.ops[2]))), func->getRegister("RR", instr.ops[0]));
}

// Load Byte

inline void lbz_e(Instruction instr, IRFunc* func)
{
    BUILD->CreateStore(zExt64(Load8(getEA_D(func, instr.ops[1], instr.ops[2]))), func->getRegister("RR", instr.ops[0]));
}
inline void lbzu_e(Instruction instr, IRFunc* func)
{
    llvm::Value* ea = getEA_D(func, instr.ops[1], instr.ops[2]);
    BUILD->CreateStore(zExt64(Load8(ea)), func->getRegister("RR", instr.ops[0]));
    updateRA_EA(func, instr, ea);
}
inline void lbzx_e(Instruction instr, IRFunc* func)
{
    BUILD->CreateStore(zExt64(Load8(getEA_R(func, instr.ops[1], instr.ops[2]))), func->getRegister("RR", instr.ops[0]));
}

// Load Double-Word

inline void ld_e(Instruction instr, IRFunc* func)
{
    BUILD->CreateStore(Load64(getEA_DWORD_D(func, instr.ops[1], instr.ops[2])), func->getRegister("RR", instr.ops[0]));
}

inline void ldu_e(Instruction instr, IRFunc* func)
{
    llvm::Value* ea = getEA_DWORD_D(func, instr.ops[1], instr.ops[2]);
    BUILD->CreateStore(Load64(ea), func->getRegister("RR", instr.ops[0]));
    updateRA_EA(func, instr, ea);
}


//
// STORE
//

// Store Word

inline void stw_e(Instruction instr, IRFunc* func)
{
    Store32(gprVal(instr.ops[0]), getEA_D(func, instr.ops[1], instr.ops[2]));
}
inline void stwu_e(Instruction instr, IRFunc* func)
{
    llvm::Value* ea = getEA_D(func, instr.ops[1], instr.ops[2]);
    Store32(gprVal(instr.ops[0]), ea)
    updateRA_EA(func, instr, ea);
}
inline void stwx_e(Instruction instr, IRFunc* func)
{
    Store32(gprVal(instr.ops[0]), getEA_R(func, instr.ops[1], instr.ops[2]));
}


// Store Half-Word

inline void sth_e(Instruction instr, IRFunc* func)
{
    Store16(gprVal(instr.ops[0]), getEA_D(func, instr.ops[1], instr.ops[2]));
}
inline void sthu_e(Instruction instr, IRFunc* func)
{
    llvm::Value* ea = getEA_D(func, instr.ops[1], instr.ops[2]);
    Store16(gprVal(instr.ops[0]), ea);
    updateRA_EA(func, instr, ea);
}
inline void sthx_e(Instruction instr, IRFunc* func)
{
    Store16(gprVal(instr.ops[0]), getEA_R(func, instr.ops[1], instr.ops[2]));
}

// Store Byte

inline void stb_e(Instruction instr, IRFunc* func)
{
    Store8(gprVal(instr.ops[0]), getEA_D(func, instr.ops[1], instr.ops[2]));
}
inline void stbu_e(Instruction instr, IRFunc* func)
{
    llvm::Value* ea = getEA_D(func, instr.ops[1], instr.ops[2]);
    Store8(gprVal(instr.ops[0]), ea);
    updateRA_EA(func, instr, ea);
}


// Store Double-Word

inline void std_e(Instruction instr, IRFunc* func)
{
    Store64(gprVal(instr.ops[0]), getEA_DWORD_D(func, instr.ops[1], instr.ops[2]));
}
inline void stdu_e(Instruction instr, IRFunc* func)
{
    llvm::Value* ea = getEA_DWORD_D(func, instr.ops[1], instr.ops[2]);
    Store64(gprVal(instr.ops[0]), ea);
    updateRA_EA(func, instr, ea);
}



inline void slw_e(Instruction instr, IRFunc* func)
{
    llvm::Value* sh = BUILD->CreateAnd(trcTo8(gprVal(instr.ops[2])), i8Const(0x3F), "and");
    llvm::Value* v = BUILD->CreateSelect(BUILD->CreateICmpULT(gprVal(instr.ops[2]), i64Const(32), "ULT"), trcTo32(BUILD->CreateShl(gprVal(instr.ops[1]), zExt64(sh), "shl")), i32Const(0), "sel");
    BUILD->CreateStore(zExt64(v), func->getRegister("RR", instr.ops[0]));
    /*if (i.X.Rc) {
        f.UpdateCR(0, v);
    }*/
}

#define DMASK(b, e) (((0xFFFFFFFF << ((31 + (b)) - (e))) >> (b)))
#define QMASK(b, e) ((0xFFFFFFFFFFFFFFFF << ((63 + (b)) - (e))) >> (b))
// please optimize this
inline void rlwinm_e(Instruction instr, IRFunc* func)
{
    uint32_t mask = (instr.ops[3] <= instr.ops[4]) ? (DMASK(instr.ops[3], instr.ops[4])) : (DMASK(0, instr.ops[4]) | DMASK(3, 31));

    uint32_t width = 32 - instr.ops[2];
    llvm::Value* lhs = BUILD->CreateShl(gprVal(instr.ops[1]), instr.ops[2], "lhs");
    llvm::Value* rhs = BUILD->CreateLShr(gprVal(instr.ops[1]), width, "rhs");
    llvm::Value* rotl = BUILD->CreateOr(lhs, rhs, "rotl");
    
    auto masked = trcTo32(BUILD->CreateAnd(rotl, i64Const(mask), "and"));
    BUILD->CreateStore(zExt64(masked), func->getRegister("RR", instr.ops[0]));
    UpdateCR_CmpZero(func, instr, "rlwinmRC", zExt64(masked));
}

inline void rlwimi_e(Instruction instr, IRFunc* func)
{
    // n <- SH
    // r <- ROTL32((RS)[32:63], n)
    // m <- MASK(MB+32, ME+32)
    // RA <- r&m | (RA)&¬m
    uint32_t width = 32 - instr.ops[2];
    llvm::Value* lhs = BUILD->CreateShl(gprVal(instr.ops[1]), instr.ops[2], "lhs");
    llvm::Value* rhs = BUILD->CreateLShr(gprVal(instr.ops[1]), width, "rhs");
    llvm::Value* rotl = BUILD->CreateOr(lhs, rhs, "rotl");
    uint64_t mask = XEMASK(instr.ops[3] + 32, instr.ops[4] + 32);
    if (mask == 0xFFFFFFFFFFFFFFFFull)
    {
        DebugBreak();
    }
    llvm::Value* result = BUILD->CreateOr(BUILD->CreateAnd(trcTo32(rotl), i32Const(mask), "and"), BUILD->CreateAnd(trcTo32(gprVal(instr.ops[0])), i32Const(~mask), "and"), "or");
    BUILD->CreateStore(result, func->getRegister("RR", instr.ops[0]));
}

inline void rldicl_e(Instruction instr, IRFunc* func)
{
    uint32_t width = 64 - instr.ops[2];
    llvm::Value* lhs = BUILD->CreateShl(gprVal(instr.ops[1]), instr.ops[2], "lhs");
    llvm::Value* rhs = BUILD->CreateLShr(gprVal(instr.ops[1]), width, "rhs");
    llvm::Value* rotl = BUILD->CreateOr(lhs, rhs, "rotl");
    uint64_t mask = QMASK(instr.ops[3], 63);
    llvm::Value* result = BUILD->CreateAnd(rotl, i64Const(mask), "and");
    BUILD->CreateStore(result, func->getRegister("RR", instr.ops[0]));
}


inline void srawi_e(Instruction instr, IRFunc* func) 
{
    // n <- SH
    // r <- ROTL32((RS)[32:63], 64-n)
    // m <- MASK(n+32, 63)
    // s <- (RS)[32]
    // RA <- r&m | (i64.s)&¬m
    // CA <- s & ((r&¬m)[32:63]≠0)
    // if n == 0: rA <- sign_extend(rS), XER[CA] = 0
    // if n >= 32: rA <- 64 sign bits of rS, XER[CA] = sign bit of lo_32(rS)
   

    llvm::Value* v = trcTo32(gprVal(instr.ops[1]));
    llvm::Value* ca;
    if (!instr.ops[2]) // if shift is 0 don't calculate the other shis
    {
        // No shift, just a fancy sign extend and CA clearer.
        v = sExt64(v);
        ca = BUILD->getInt8(0);
    }
    else 
    {
        // CA is set if any bits are shifted out of the right and if the result
        // is negative.
        uint32_t mask = (uint32_t)XEMASK(64 - instr.ops[2], 63);

        if (mask == 1) 
        {
            ca = BUILD->CreateAnd(BUILD->CreateICmpSLT(v, i32Const(0), "slt"), trcTo1(v), "and");
        }
        else {
            ca = BUILD->CreateAnd(BUILD->CreateICmpSLT(v, i32Const(0), "slt"),
                BUILD->CreateICmpNE(BUILD->CreateAnd(v, i32Const(mask), "and"), i32Const(0), "cmp"), "and");
        }

        //v = f.Sha(v, (int8_t)instr.ops[2]), v = sExt64(v);
        v = BUILD->CreateAShr(v, i32Const(instr.ops[2]), "ashr"), v = sExt64(v);
    }

    StoreCA(func, ca);

    BUILD->CreateStore(v, func->getRegister("RR", instr.ops[0]));
    /*if (i.X.Rc) {
        f.UpdateCR(0, v);
    }*/
}

// AHHHHHHH instrinsic
inline void cntlzw_e(Instruction instr, IRFunc* func)
{
    llvm::Function* CtlzFunc = llvm::Intrinsic::getDeclarationIfExists(func->m_irGen->m_module, llvm::Intrinsic::ctlz, { i32_T });
    llvm::Value* IsZeroUndef = i1Const(false);  // Do not allow undef
    llvm::Value* LeadingZeros = BUILD->CreateCall(CtlzFunc, { trcTo32(gprVal(instr.ops[1])), IsZeroUndef}, "call");
    BUILD->CreateStore(LeadingZeros, func->getRegister("RR", instr.ops[0]));
}

//
//// bitwise operators
//

inline void orx_e(Instruction instr, IRFunc* func)
{
    // The contents of rS are ORed with the contents of rB and the result is placed into rA.
    llvm::Value* value = BUILD->CreateOr(gprVal(instr.ops[1]), gprVal(instr.ops[2]), "or");
    BUILD->CreateStore(value, func->getRegister("RR", instr.ops[0]));
    UpdateCR_CmpZero(func, instr, "orRC", value);
}

inline void ori_e(Instruction instr, IRFunc* func)
{
    auto im64 = i64Const(instr.ops[2]);
    auto orResult = BUILD->CreateOr(gprVal(instr.ops[1]), im64, "or");
    BUILD->CreateStore(orResult, func->getRegister("RR", instr.ops[0]));
}

inline void oris_e(Instruction instr, IRFunc* func)
{
    auto im64 = zExt64(i32Const(instr.ops[2] << 16));
    auto orResult = BUILD->CreateOr(gprVal(instr.ops[1]), im64, "or");
    BUILD->CreateStore(orResult, func->getRegister("RR", instr.ops[0]));
}

inline void and_e(Instruction instr, IRFunc* func)
{
    auto andResult = BUILD->CreateAnd(gprVal(instr.ops[1]), gprVal(instr.ops[2]), "and");
    BUILD->CreateStore(andResult, func->getRegister("RR", instr.ops[0]));
}

inline void andc_e(Instruction instr, IRFunc* func)
{
    auto andResult = BUILD->CreateAnd(gprVal(instr.ops[1]), BUILD->CreateNot(gprVal(instr.ops[2]), "not"), "and");
    BUILD->CreateStore(andResult, func->getRegister("RR", instr.ops[0]));
}

inline void andiRC_e(Instruction instr, IRFunc* func)
{
    auto andResult = BUILD->CreateAnd(gprVal(instr.ops[1]), zExt64(i16Const(instr.ops[2])), "and");
    BUILD->CreateStore(andResult, func->getRegister("RR", instr.ops[0]));
    UpdateCR_CmpZero(func, instr, "andiRC", andResult);
}

inline void xor_e(Instruction instr, IRFunc* func)
{
    auto xorResult = BUILD->CreateXor(gprVal(instr.ops[1]), gprVal(instr.ops[2]), "xor");
    BUILD->CreateStore(xorResult, func->getRegister("RR", instr.ops[0]));
}

inline void xori_e(Instruction instr, IRFunc* func)
{
    auto xorResult = BUILD->CreateXor(gprVal(instr.ops[1]), zExt64(i16Const( instr.ops[2])), "xor");
    BUILD->CreateStore(xorResult, func->getRegister("RR", instr.ops[0]));
}

inline void neg_e(Instruction instr, IRFunc* func)
{
    llvm::Value* negVal = BUILD->CreateNeg(gprVal(instr.ops[1]), "neg");
    BUILD->CreateStore(negVal, func->getRegister("RR", instr.ops[0]));
}

inline void nor_e(Instruction instr, IRFunc* func)
{
    llvm::Value* norVal = BUILD->CreateNeg(BUILD->CreateOr(gprVal(instr.ops[1]), gprVal(instr.ops[2]), "or"), "neg");
    BUILD->CreateStore(norVal, func->getRegister("RR", instr.ops[0]));
}



//
//// MATH
//

inline void mullw_e(Instruction instr, IRFunc* func)
{
    auto mulResult = trcTo32(BUILD->CreateMul(gprVal(instr.ops[1]), gprVal(instr.ops[2]), "Mul"));
    BUILD->CreateStore(mulResult, func->getRegister("RR", instr.ops[0]));
    UpdateCR_CmpZero(func, instr, "mullwRC", zExt64(mulResult));
}

inline void mulld_e(Instruction instr, IRFunc* func)
{
    auto mulResult = trcTo64(BUILD->CreateMul(gprVal(instr.ops[1]), gprVal(instr.ops[2]), "Mul"));
    BUILD->CreateStore(mulResult, func->getRegister("RR", instr.ops[0]));
}

inline void mulli_e(Instruction instr, IRFunc* func)
{
    auto mulResult = BUILD->CreateMul(gprVal(instr.ops[1]), sign64(instr.ops[2]), "Mul");
    BUILD->CreateStore(mulResult, func->getRegister("RR", instr.ops[0]));
}


inline void divwx_e(Instruction instr, IRFunc* func) 
{
    llvm::Value* divisor = trcTo32(gprVal(instr.ops[2]));
    llvm::Value* v = BUILD->CreateSDiv(trcTo32(gprVal(instr.ops[1])), divisor, "div");
    v = zExt64(v);
    BUILD->CreateStore(v, func->getRegister("RR", instr.ops[0]));
    /*if (i.XO.Rc) {
        f.UpdateCR(0, v);
    }*/
}

inline void divwux_e(Instruction instr, IRFunc* func)
{
    llvm::Value* divisor = trcTo32(gprVal(instr.ops[2]));
    llvm::Value* v = BUILD->CreateUDiv(trcTo32(gprVal(instr.ops[1])), divisor, "div");
    v = zExt64(v);
    BUILD->CreateStore(v, func->getRegister("RR", instr.ops[0]));
    /*if (i.XO.Rc) {
        f.UpdateCR(0, v);
    }*/
}

inline void divdu_e(Instruction instr, IRFunc* func)
{
    llvm::Value* divisor = gprVal(instr.ops[2]);
    llvm::Value* v = BUILD->CreateUDiv(gprVal(instr.ops[1]), divisor, "div");
    BUILD->CreateStore(v, func->getRegister("RR", instr.ops[0]));
    /*if (i.XO.Rc) {
        f.UpdateCR(0, v);
    }*/
}

inline void subf_e(Instruction instr, IRFunc* func)
{
    // in docs the operation is:
    // rD ← ~ (rA) + (rB) + 1
    // but can be simplified to -> rB - rA, THEY ARE SWAPPED
    llvm::Value* v = BUILD->CreateSub(gprVal(instr.ops[2]), gprVal(instr.ops[1]), "sub");
    BUILD->CreateStore(v, func->getRegister("RR", instr.ops[0]));
    UpdateCR_CmpZero(func, instr, "subfRC", v);
}

inline void subfe_e(Instruction instr, IRFunc* func)
{
    // in docs the operation is:
    // rD ← ~ (rA) + (rB) + 1
    // but can be simplified to -> rB - rA, THEY ARE SWAPPED
    llvm::Value* v = BUILD->CreateSub(gprVal(instr.ops[2]), gprVal(instr.ops[1]), "sub");
    llvm::Value* vXer = BUILD->CreateAdd(v, zExt64(getCA(func)), "valXer");
    BUILD->CreateStore(vXer, func->getRegister("RR", instr.ops[0]));
    UpdateCR_CmpZero(func, instr, "subfeRC", v);
}

inline void subfic_e(Instruction instr, IRFunc* func)
{
    llvm::Value* imm = i64Const(static_cast<int16_t>(instr.ops[2]));
    llvm::Value* v = BUILD->CreateSub(imm, gprVal(instr.ops[1]), "sub");
    BUILD->CreateStore(v, func->getRegister("RR", instr.ops[0]));
    StoreCA(func, SubCarried(func, gprVal(instr.ops[1]), imm));
}