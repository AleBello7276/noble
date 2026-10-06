#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace config {

enum class JITPreset { Development, Performance, Validation };
enum class Compilation { Blocks, Functions };
enum class Optimization { None, Speed, SpeedAndSize };
enum class RegisterAllocation { Backtracking, SinglePass };

// immutable session settings copied by the jit before guest workers start
struct JITConfig {
    JITPreset preset = JITPreset::Development;
    Compilation compilation = Compilation::Blocks;
    Optimization optimization = Optimization::None;
    RegisterAllocation registerAllocation = RegisterAllocation::Backtracking;
    bool aliasAnalysis = true;
    bool preserveFramePointers = false;
    uint32_t minFunctionAlignmentLog2 = 0;
    bool verifyPasses = false;
    bool dumpIR = false;
    uint32_t branchBudget = 1024;
    // additional cranelift settings are validated by cranelift during initialization
    std::map<std::string, std::string> flags;
};

struct DiagnosticsConfig {
    bool profiling = false;
    bool executionTrace = false;
};

struct DebuggerConfig {
    bool enabled = false;
    bool breakOnEntry = true;
};

// components receive only the settings they need and never depend on the file parser
struct Settings {
    JITConfig jit;
    DiagnosticsConfig diagnostics;
    DebuggerConfig debugger;
};

// map typed choices to the names used in configuration files and cranelift flags
const char* Name(JITPreset value);
const char* Name(Compilation value);
const char* Name(Optimization value);
const char* Name(RegisterAllocation value);

}  // namespace config
