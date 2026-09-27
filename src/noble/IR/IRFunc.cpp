#include "IRFunc.h"
#include <sstream>
#include <unordered_set>




//
// Code Blocks
//



inline uint32_t signExtend(uint32_t value, int size)
{
    if (value & (1 << (size - 1))) {
        return value | (~0 << size);
    }
    return value;
}

bool IRFunc::EmitFunction()
{
    bool result;
    m_irGen->m_builder->SetInsertPoint(getCreateBBinMap(start_address));

    uint32_t idx = this->start_address;
    if (start_address == 0x82014DA8) DebugBreak();

    // discover start basic blocks
    while (idx <= this->end_address)
    {
		Instruction instr = m_irGen->instrsList.at(idx);
		if (strcmp(instr.opcName.c_str(), "b") == 0)
		{
            uint32_t target = idx + signExtend(instr.ops[0], 24);
            // check for tail calls
            if (!m_irGen->isIRFuncinMap(target))
            {
                this->getCreateBBinMap(target);
                //this->getCreateBBinMap(instr.address + 4);
            }
		}
        if (strcmp(instr.opcName.c_str(), "bc") == 0)
        {
            this->getCreateBBinMap(instr.address + (int16_t)(instr.ops[2] << 2));
            this->getCreateBBinMap(instr.address + 4);
        }


        idx += 4;
    }

	CodeBlock* currentBlock = codeBlocks.at(start_address);
    idx = this->start_address;
	// discover end basic blocks
    while (idx <= this->end_address)
    {
        Instruction instr = m_irGen->instrsList.at(idx);
		if (isBBinMap(idx + 4))
		{
			currentBlock->end = idx;
			currentBlock = codeBlocks.at(idx + 4);
		}
		if (strcmp(instr.opcName.c_str(), "bclr") == 0)
		{
			currentBlock->end = idx;
		}
        if(idx == this->end_address)
        {
            currentBlock->end = this->end_address;
        }
        idx += 4;
    }

    // can be optimized?
    // TODO

    // emit
    idx = this->start_address;
    while (idx <= this->end_address)
    {
		if (isBBinMap(idx))
		{
			m_irGen->m_builder->SetInsertPoint(codeBlocks.at(idx)->bb_Block);

            CodeBlock* block = codeBlocks.at(idx);


            uint32_t blockIdx = block->address;
            while (blockIdx <= block->end)
            {
                Instruction instr = m_irGen->instrsList.at(blockIdx);

                if (!m_irGen->EmitInstruction(m_irGen->instrsList.at(blockIdx), this))
                {
                    __debugbreak();
                    return 1;
                }

                if (blockIdx != this->end_address && blockIdx == block->end && strcmp(instr.opcName.c_str(), "bclr") != 0 )
                {
                    m_irGen->m_builder->CreateBr(codeBlocks.at(block->end + 4)->bb_Block);
                }
                blockIdx += 4;
                
            }

			idx = blockIdx;
		}
    }


    return true;
}

void IRFunc::genBody()
{
    std::ostringstream oss{};
    oss << "func_" << std::hex << std::setfill('0') << std::setw(8) << start_address;

    llvm::FunctionType* mainType = llvm::FunctionType::get(m_irGen->m_builder->getVoidTy(), {m_irGen->XenonStateType->getPointerTo(), m_irGen->m_builder->getInt32Ty()}, false);
    m_irFunc = llvm::Function::Create(mainType, llvm::Function::ExternalLinkage, oss.str(), m_irGen->m_module);

    getCreateBBinMap(start_address);
    //m_irGen->m_builder->SetInsertPoint(getCreateBBinMap(start_address));

}

llvm::BasicBlock* IRFunc::createBasicBlock(uint32_t address)
{
    std::ostringstream oss{};
    oss << "bb_" << std::hex << std::setfill('0') << std::setw(8) << address;
    return llvm::BasicBlock::Create(m_irGen->m_module->getContext(), oss.str(), m_irFunc);
}

llvm::BasicBlock* IRFunc::getCreateBBinMap(uint32_t address)
{
    if (isBBinMap(address)) {
        return codeBlocks.at(address)->bb_Block;
    }
    CodeBlock* block = new CodeBlock();
    block->bb_Block = createBasicBlock(address);
	block->address = address;
    codeBlocks.try_emplace(address, block);
    return block->bb_Block;
}

bool IRFunc::isBBinMap(uint32_t address)
{
    return codeBlocks.find(address) != codeBlocks.end();
}


//
// LR   -> 0
// CTR  -> 1
// MSR  -> 2
// XER  -> 3
// CR   -> 4
// RR   -> 5
// FR   -> 6
// VX   -> 7
//

llvm::Value* IRFunc::getRegister(const std::string& regName, int index1, int index2)
{
    auto argIter = this->m_irFunc->arg_begin();
    llvm::Argument* xCtx = &*argIter;


    if (regName == "LR" || regName == "CTR" || regName == "MSR" || regName == "XER")
    {
        int fieldIndex = (regName == "LR") ? 0 :
            (regName == "CTR") ? 1 :
            (regName == "MSR") ? 2 : 3;


        return m_irGen->m_builder->CreateStructGEP(m_irGen->XenonStateType, xCtx, fieldIndex, "spr_ptr");
    }
    else if (regName == "CR")
    {
        return m_irGen->m_builder->CreateStructGEP(m_irGen->XenonStateType, xCtx, 4, "reg.CR");
    }
    else if (regName == "RR")
    {
        assert(index1 != -1 && "Index for RR must be provided.");
        return m_irGen->m_builder->CreateGEP(
            llvm::Type::getInt64Ty(m_irGen->m_builder->getContext()),
            m_irGen->m_builder->CreateStructGEP(m_irGen->XenonStateType, xCtx, 5, "reg.RR"),
            llvm::ConstantInt::get(m_irGen->m_builder->getInt32Ty(), index1),
            "reg.RR[" + std::to_string(index1) + "]");
    }
    else if (regName == "FR")
    {
        assert(index1 != -1 && "Index for FR must be provided.");
        return m_irGen->m_builder->CreateGEP(
            llvm::Type::getInt64Ty(m_irGen->m_builder->getContext()),
            m_irGen->m_builder->CreateStructGEP(m_irGen->XenonStateType, xCtx, 6, "reg.FR"),
            llvm::ConstantInt::get(m_irGen->m_builder->getInt32Ty(), index1),
            "reg.FR[" + std::to_string(index1) + "]");
    }
    else {
        llvm::errs() << "Unknown register name: " << regName << "\n";
        return nullptr;
    }
}

llvm::Value* IRFunc::getSPR(uint32_t n)
{
    uint32_t spr4 = (n & 0b1111100000) >> 5;
    uint32_t spr9 = n & 0b0000011111;

    if (spr4 == 1) return this->getRegister("XER");
    if (spr4 == 8) return this->getRegister("LR");
    if (spr4 == 9) return this->getRegister("CTR");
    return NULL;
}