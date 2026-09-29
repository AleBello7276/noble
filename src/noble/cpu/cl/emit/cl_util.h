#pragma once

#include "Logger.h"
#include "cpu/cl/CraneliftJIT.h"
#include "cranelift.h"
#include <assert.h>

using EmitterHandler = void (*)(EmitterContext& emitter_, InstructionInfo& info_);

#define CLHandler(name) inline void cl_##name##_handler(EmitterContext& emitter_, InstructionInfo& info_)
