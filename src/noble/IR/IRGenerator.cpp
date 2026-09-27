#include "IRGenerator.h"
#include "InstructionEmitter.h"
#include <format>
#include <iomanip>
#include <sstream>



IRGenerator::IRGenerator(llvm::Module* mod, llvm::IRBuilder<llvm::NoFolder>* builder)
  : m_builder(builder)
  , m_module(mod) {
  
}


void IRGenerator::CxtSwapFunc()
{
    tlsVariable = new llvm::GlobalVariable(
        *m_module,
        XenonStateType->getPointerTo(),
        false,
        llvm::GlobalValue::ExternalLinkage,
        llvm::ConstantPointerNull::get(XenonStateType->getPointerTo()),
        "xCtx",
        nullptr,
        llvm::GlobalValue::GeneralDynamicTLSModel, 
        0
    );

    // getXCtxAddress func
    llvm::Type* retType = tlsVariable->getType();

    llvm::FunctionType* funcType = llvm::FunctionType::get(retType, false);
    llvm::Function* getXCtxAddressFn = llvm::Function::Create(
        funcType,
        llvm::GlobalValue::ExternalLinkage,
        "getXCtxAddress",
        m_module
    );

    getXCtxAddressFn->setDLLStorageClass(llvm::GlobalValue::DLLExportStorageClass);

    llvm::BasicBlock* entryBlock = llvm::BasicBlock::Create(m_builder->getContext(), "entry", getXCtxAddressFn);
    m_builder->SetInsertPoint(entryBlock);
    m_builder->CreateRet(tlsVariable);
}

void IRGenerator::initExtFunc()
{
    llvm::FunctionType* importType = llvm::FunctionType::get(m_builder->getVoidTy(), { XenonStateType->getPointerTo(), m_builder->getInt32Ty() }, false);
    bcctrlFunc = llvm::Function::Create(importType, llvm::Function::ExternalLinkage, "HandleBcctrl", m_module);

    // XenonState, instrAddress, name
    llvm::FunctionType* callBkType = llvm::FunctionType::get(m_builder->getVoidTy(), { XenonStateType->getPointerTo(), m_builder->getInt32Ty(),  m_builder->getInt8Ty()->getPointerTo() }, false);
    dBCallBackFunc = llvm::Function::Create(callBkType, llvm::Function::ExternalLinkage, "DebugCallBack", m_module);

    llvm::FunctionType* funcType = llvm::FunctionType::get(m_builder->getVoidTy(),false);
    dllTestFunc = llvm::Function::Create(
        funcType,
        llvm::Function::ExternalLinkage,
        "dllHack",
        m_module
    );
}

void IRGenerator::InitLLVM() {
  
    CxtSwapFunc();
    initExtFunc();

    
    module_base = new llvm::GlobalVariable(
        *m_module,                      
        m_builder->getInt64Ty(),             
        false,                  
        llvm::GlobalValue::ExternalLinkage, 
        m_builder->getInt64(0),
        "moduleBase"
    );
    module_base->setDLLStorageClass(llvm::GlobalValue::DLLExportStorageClass);

    // intrinsics types
    swap16 = llvm::Intrinsic::getDeclarationIfExists(m_module, llvm::Intrinsic::bswap, m_builder->getInt16Ty());
    swap32 = llvm::Intrinsic::getDeclarationIfExists(m_module, llvm::Intrinsic::bswap, m_builder->getInt32Ty());
    swap64 = llvm::Intrinsic::getDeclarationIfExists(m_module, llvm::Intrinsic::bswap, m_builder->getInt64Ty());


	// main function / entry point
    llvm::FunctionType* mainType = llvm::FunctionType::get(m_builder->getInt32Ty(), false);
    mainFn = llvm::Function::Create(mainType, llvm::Function::ExternalLinkage, "main", m_module);
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(m_module->getContext(), "Entry", mainFn);
    m_builder->SetInsertPoint(entry);

    xCtx = m_builder->CreateLoad(
        tlsVariable->getType()->getPointerTo(),
        tlsVariable,
        "xCtx"
    );

    //
    // JUMP TO XEX ENTRY POINT
    //

    // add xex entry point function
	//IRFunc* xex_entry = getCreateFuncInMap(m_xexImage->GetEntryAddress());
	
    IRFunc* xex_entry = getCreateFuncInMap(0x0);

    initFuncBody(xex_entry);
    m_builder->SetInsertPoint(entry);
    m_builder->CreateCall(dllTestFunc);
	//m_builder->CreateCall(xex_entry->m_irFunc, { xCtx, llvm::ConstantInt::get(m_builder->getInt32Ty(), m_xexImage->GetEntryAddress())});
    m_builder->CreateCall(xex_entry->m_irFunc, { xCtx, llvm::ConstantInt::get(m_builder->getInt32Ty(), 0x01) });

    
	m_builder->SetInsertPoint(entry);
    
    m_builder->CreateCall(dllTestFunc);

	m_builder->CreateRet(llvm::ConstantInt::get(m_builder->getInt32Ty(), 0));

	
}

void IRGenerator::exportFunctionArray() 
{

    llvm::Type* i32Ty = m_builder->getInt32Ty();
    llvm::Type* i8PtrTy = m_builder->getInt8Ty()->getPointerTo();

    //
    // Count
    //
    llvm::Constant* countConst = llvm::ConstantInt::get(i32Ty, m_function_map.size());
    llvm::GlobalVariable* countGV = new llvm::GlobalVariable(
        *m_module,
        i32Ty,
        true,                              
        llvm::GlobalValue::ExternalLinkage, 
        countConst,                        
        "X_FunctionArrayCount"            
    );
    
    //
    // Array
    //
    std::vector<llvm::Type*> fieldTypes = { i32Ty, i8PtrTy };
    llvm::StructType* xFuncType = llvm::StructType::create(m_module->getContext(), fieldTypes, "X_Function");
    std::vector<llvm::Constant*> initElements;
    for (const auto& pair : m_function_map)
    {
        IRFunc* func = pair.second;
        llvm::Constant* addrConst = llvm::ConstantInt::get(i32Ty, func->start_address, false);
        llvm::Constant* funcPtr = llvm::ConstantExpr::getBitCast(func->m_irFunc, i8PtrTy);
        std::vector<llvm::Constant*> structFields = { addrConst, funcPtr };
        llvm::Constant* xFuncConst = llvm::ConstantStruct::get(xFuncType, structFields);
        initElements.push_back(xFuncConst);
    }


    llvm::ArrayType* arrType = llvm::ArrayType::get(xFuncType, initElements.size());
    llvm::Constant* arrInit = llvm::ConstantArray::get(arrType, initElements);
    llvm::GlobalVariable* exportArrGV = new llvm::GlobalVariable(
        *m_module,
        arrType,
        true,  
        llvm::GlobalValue::ExternalLinkage,
        arrInit,
        "X_FunctionArray"
    );

    countGV->setDLLStorageClass(llvm::GlobalValue::DLLExportStorageClass);
    exportArrGV->setDLLStorageClass(llvm::GlobalValue::DLLExportStorageClass);
}

void IRGenerator::Initialize() {
   InitLLVM();
}

bool first = true;

#define DEBUG_COMMENT(x) m_builder->CreateAdd(m_builder->getInt32(0), m_builder->getInt32(0), x);
#define DEBUG_CALLBACK() m_builder->CreateCall(dBCallBackFunc, { &*func->m_irFunc->arg_begin(), m_builder->getInt32(instr.address), m_builder->CreateGlobalStringPtr(instr.opcName) });

bool IRGenerator::EmitInstruction(Instruction instr, IRFunc* func) {
    // EEHHE can't use a std::string in a switch statement sooo...
    // let's map all the instruction names (as i already needed to do with a switch statement)
    // in a vector, so i can look up the instr opcode name in that vector and call the instruction
    // emitter
    // all emitter functions take as parameter <Instruction, IRGenerator>

	// Debug, help to find the instruction and debug IR code
    std::ostringstream oss;
    oss << std::hex << std::uppercase << std::setfill('0');
    oss << "------ " << std::setw(8) << instr.address << ":   " << instr.opcName;

    for (size_t i = 0; i < instr.ops.size(); ++i) {
        oss << " " << std::setw(2) << static_cast<int>(instr.ops.at(i));
    }
    oss << " ------";
    DEBUG_COMMENT(oss.str().c_str())

    if (m_dbCallBack)
    {
        DEBUG_CALLBACK();
    }

        // <name>_e = <name>_emitter
    static std::unordered_map<std::string, std::function<void(Instruction, IRFunc*)>>
        instructionMap =
    {
         {"nop", nop_e },
         {"twi", twi_e },
         {"tdi", tdi_e},
         {"mfspr", mfspr_e },
         {"mfcr", mfcr_e}, //
         {"stw", stw_e },
         {"stwu", stwu_e },
         {"lis", addis_e }, // it's a simplified mnemonic
         {"addis", addis_e },
         {"li", addi_e },
         {"addi", addi_e },
         {"lwz", lwz_e },
         {"lwzu", lwzu_e },
         {"lwzx", lwzx_e},
         {"mtspr", mtspr_e },
         {"or", orx_e },
         {"orRC", orx_e},
         {"sth", sth_e },
         {"sthu", sthu_e},
         {"sthx", sthx_e},
         {"b", b_e },
         {"bl", bl_e },
         {"bclr", bclr_e },
         {"bcctrl", bcctrl_e},
         {"lhz", lhz_e },
         {"lhzu", lhzu_e},
         {"lha", lha_e},
         {"lhzx", lhzx_e},
         {"cmpw", cmpw_e},
         {"bc", bcx_e},
         {"add", add_e},
         {"ori", ori_e},
         {"cmpwi", cmpi_e},
         {"cmpdi", cmpi_e},
         {"neg", neg_e},
         {"and", and_e},
         {"xor", xor_e},
         {"rlwinmRC", rlwinm_e},
         {"rlwinm", rlwinm_e},
         {"mullw", mullw_e},
         {"mullwRC", mullw_e},
         {"srawi", srawi_e},
         {"divw", divwx_e},
         {"andc", andc_e},
         {"subf", subf_e},
         {"subfe", subfe_e},
         {"subfRC", subf_e},
         {"subfeRC", subfe_e},
         {"stwx", stwx_e},
         {"cmplwi", cmpli_e},
         {"mulli", mulli_e},
         {"std", std_e},
         {"stdu", stdu_e},
         {"lbz", lbz_e},
         {"lbzu", lbzu_e},
         {"lbzx", lbzx_e},
         {"bcctr", bcctr_e},
         {"xori", xori_e},
         {"nor", nor_e},
         {"cntlzw", cntlzw_e},
         {"andiRC", andiRC_e},
         {"stb", stb_e},
         {"stbu", stbu_e},
         {"extsw", extsw_e},
         {"extswRC", extsw_e},
         {"extsh", extsh_e}, 
         {"extshRC", extsh_e}, 
         {"extsb", extsb_e}, //
         {"extsbRC", extsb_e}, //
         {"cmplw", cmpl_e}, //
         {"ld", ld_e},
         {"adde", adde_e}, //
         {"addic", addic_e},
         {"addicRC", addic_e}, //
         {"slw", slw_e}, //
         {"adde", adde_e}, //
         {"addze", addze_e},
         {"addzeRC", addze_e},
         {"oris", oris_e},
         {"rlwimi", rlwimi_e},
         {"subfic", subfic_e},
         {"rldicl", rldicl_e},
         {"lwa", lwa_e},
         {"divdu", divdu_e},
         {"divwu", divwux_e},
         {"mulld", mulld_e},
         {"dcbt", dcbt_e},
         {"dcbtst", dcbtst_e},
         {"ldu", ldu_e},

         {"cmpldi", cmpli_e},
    };


  if (instructionMap.find(instr.opcName) != instructionMap.end()) {
    instructionMap[instr.opcName](instr, func);
    return true;
  } else {
        printf("Instruction:   %s  not Implemented\n", instr.opcName.c_str());
  }

  writeIRtoFile();

  return false;
}




void IRGenerator::writeIRtoFile()
{
    

    printf("----IR DUMP----\n\n\n");
    if (m_dumpIRConsole)
    {
        llvm::raw_fd_ostream& out = llvm::outs();
        m_module->print(out, nullptr);
    }
    

    std::error_code EC;
    llvm::raw_fd_ostream OS("../../bin/Debug/output.ll", EC);
    if (EC) {
        llvm::errs() << "Error writing to file: " << EC.message() << "\n";
    }
    else {
        m_module->print(OS, nullptr);
    }
    OS.close();
}



//
// Func Helpers
//

void IRGenerator::initFuncBody(IRFunc* func)
{
	if (func->m_irFunc != nullptr) 
    {
		printf("Function Body already exist\n");
        return;
	}

    func->genBody();
}

IRFunc* IRGenerator::getCreateFuncInMap(uint32_t address)
{
    if (isIRFuncinMap(address)) {
        return m_function_map.at(address);
    }
    IRFunc* func = new IRFunc();
	func->start_address = address;
    func->end_address = NULL;
	func->m_irGen = this;
    m_function_map.try_emplace(address, func);
    return func;
}

bool IRGenerator::isIRFuncinMap(uint32_t address)
{
    return m_function_map.find(address) != m_function_map.end();
}