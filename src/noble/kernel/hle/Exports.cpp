#include "Exports.h"

#include "krnl/Debug.h"

#include "krnl/Memory.h"
#include "krnl/IO.h"
#include "krnl/Modules.h"
#include "krnl/Objects.h"
#include "krnl/Rtl.h"
#include "krnl/Threading.h"
#include "krnl/Variables.h"
#include "krnl/Video.h"
#include "krnl/XConfig.h"
#include "xam/Input.h"
#include "xam/Video.h"
#include <array>
#include <unordered_set>

namespace hle {

void RegisterExports(Registry& registry) {
    const std::array groups{krnl::ThreadingExports(), krnl::RtlExports(),   krnl::MemoryExports(),
                            krnl::ModuleExports(),    krnl::VideoExports(), xam::VideoExports(),
                            krnl::XConfigExports(),   krnl::ObjectExports(), krnl::IOExports(),
                            krnl::DebugExports(),     xam::InputExports()};

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

    krnl::RegisterVariables(registry);
}

}  // namespace hle
