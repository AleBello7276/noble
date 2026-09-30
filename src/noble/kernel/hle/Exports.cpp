#include "Exports.h"

#include "krnl/Threading.h"
#include <array>
#include <unordered_set>

namespace hle {

void RegisterExports(Registry& registry) {
    const std::array groups{krnl::ThreadingExports()};

    std::unordered_set<uint64_t> registered;

    // validate all tables before installing any entries
    for (const auto group : groups) {
        for (const auto& entry : group) {
            const uint64_t key = (uint64_t(entry.library) << 32) | entry.ordinal;
            
            if (!registered.insert(key).second)
                throw std::logic_error("duplicate hle export binding");
        }
    }

    for (const auto group : groups)
        for (const auto& entry : group)
            entry.install(registry, entry.library, entry.ordinal);
}

}  // namespace hle
