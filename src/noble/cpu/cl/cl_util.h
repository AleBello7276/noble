#pragma once

#include "CraneliftJIT.h"
#include "Logger.h"
#include "cranelift.h"
#include <assert.h>

using EmitterHandler = void (*)(IRFunc& func, InstructionInfo& info);

#define CLHandler(name) inline void cl_##name##_handler(IRFunc& func, InstructionInfo& info)
