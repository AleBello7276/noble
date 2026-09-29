#pragma once

#include "emit/cl_emitters.h"
#include "powerpc-rs.h"

using namespace std;  // only for to_underlying honestly
inline constexpr auto emitter_dispatch_table = [] {
    std::array<EmitterHandler, to_underlying(PpcOpcode::Count)> table{};
    table.fill(&cl_illegal_handler);  // init

    table[to_underlying(PpcOpcode::Mfspr)] = &cl_mfspr_handler;

    return table;
}();
