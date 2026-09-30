#pragma once

#include "Loader/table/ImportTable.h"
#include "Shims.h"
#include <span>

namespace hle {

// describe one guest export and the ordinary c++ function used to implement it
struct Export {
    XboxLibrary library;
    uint16_t ordinal;
    std::string_view name;
    void (*install)(Registry&, XboxLibrary, uint16_t);
};

// validate an export name at compile time and create its typed registration entry
template <auto Function>
consteval Export Bind(XboxLibrary library, std::string_view name) {
    for (const auto& entry : XLoader::importTable) {
        if (entry.library == library && entry.name == name && entry.type == ImportType::Function) {
            return {library, entry.ordinal, entry.name,
                    [](Registry& registry, XboxLibrary targetLibrary, uint16_t ordinal) {
                        registry.Register<Function>(targetLibrary, ordinal);
                    }};
        }
    }

    throw "hle export name is missing from the import catalogue";
}

void RegisterExports(Registry& registry);

}  // namespace hle
