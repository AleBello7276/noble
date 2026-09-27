#pragma once
#include <IR/InstructionEmitter.h>
//#include "../InstructionEmitter.h"

//
// This header contains all the various unit tests of every single insturction to ensure
// it the instruction works exaclty how it should
// i'll add unit tests each time i implement a new instruction emitter
// i can view the register dump via the runtime CpuContext dumper
//

#define UNIT(instrName, func, ops) \
	Instruction instr; \
	instr.ops = ops; \
	instrName##_e(instr, func); \
