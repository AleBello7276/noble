#pragma once
#include <string>
#include <iomanip>
#include "IRGenerator.h"

struct CodeBlock
{
	uint32_t address;
    uint32_t end;
	llvm::BasicBlock* bb_Block;
};

class IRFunc {
public:
    uint32_t start_address;
    uint32_t end_address;
    bool emission_done;
    std::unordered_map<uint32_t, CodeBlock*> codeBlocks;
    llvm::Function* m_irFunc;

    bool EmitFunction();
    void genBody();

    llvm::BasicBlock* createBasicBlock(uint32_t address);
    llvm::BasicBlock* getCreateBBinMap(uint32_t address);
    bool isBBinMap(uint32_t address);
    llvm::Value* getRegister(const std::string& regName, int arrayIndex = -1, int index2 = -1);
    llvm::Value* getSPR(uint32_t n);

    IRGenerator* m_irGen;

public:
    //
    // Metadata for bounds analyser
    //
    bool startW_MFSPR_LR;
    bool is_promotion;
    bool has_jumpTable;
	
};