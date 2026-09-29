#pragma once

#include "Loader/ImageLoader.h"
#include <algorithm>
#include <iterator>
#include <string_view>
#include <utility>

namespace XLoader {

struct ImportDefinition {
    XboxLibrary library;
    uint16_t ordinal;
    ImportType type;
    std::string_view name;
};

inline constexpr ImportDefinition importTable[] = {
#include "ImportTableEntries.inc"
};

inline bool EqualLibraryName(std::string_view left, std::string_view right) {
    if (left.size() != right.size())
        return false;

    for (size_t i = 0; i < left.size(); ++i) {
        auto lower = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c; };
        if (lower(left[i]) != lower(right[i]))
            return false;
    }

    return true;
}

inline XboxLibrary LibraryFromName(std::string_view name) {
    if (EqualLibraryName(name, "xboxkrnl.exe") || EqualLibraryName(name, "xboxkrnl"))
        return XboxLibrary::XboxKrnl;

    if (EqualLibraryName(name, "xam.xex") || EqualLibraryName(name, "xam"))
        return XboxLibrary::Xam;

    if (EqualLibraryName(name, "xbdm.xex") || EqualLibraryName(name, "xbdm.dll")
        || EqualLibraryName(name, "xbdm"))
        return XboxLibrary::Xbdm;

    if (EqualLibraryName(name, "xapi.xex") || EqualLibraryName(name, "xapi.dll")
        || EqualLibraryName(name, "xapi"))
        return XboxLibrary::Xapi;

    return XboxLibrary::Unknown;
}

inline const ImportDefinition* FindImport(XboxLibrary library, uint16_t ordinal) {
    const auto key = std::pair{library, ordinal};

    const auto it = std::lower_bound(std::begin(importTable), std::end(importTable), key,
                                     [](const ImportDefinition& entry, const auto& value) {
                                         return std::pair{entry.library, entry.ordinal} < value;
                                     });

    return it != std::end(importTable) && it->library == library && it->ordinal == ordinal ? it : nullptr;
}

inline const ImportDefinition* FindImport(std::string_view library, uint16_t ordinal) {
    return FindImport(LibraryFromName(library), ordinal);
}

}  // namespace XLoader
