#pragma once

#include "cl_emitters.h"
#include "powerpc-rs.h"

inline constexpr auto emitter_dispatch_table = [] {
    std::array<EmitterHandler, static_cast<std::size_t>(PpcOpcode::Count)> table{};
    table.fill(&cl_illegal_handler);  // init

    return table;
}();
