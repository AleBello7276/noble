#pragma once

#include "kernel/KernelTypes.h"
#include "kernel/hle/Exports.h"

namespace hle::krnl {

// decommit guest pages or release a complete reservation and return its actual range
XNTSTATUS NtFreeVirtualMemory(Memory& memory, Pointer<be<uint32_t>, PointerValidation::Report> baseAddress,
                              Pointer<be<uint32_t>, PointerValidation::Report> regionSize, uint32_t freeType,
                              uint32_t debugMemory);

std::span<const Export> MemoryExports();

}  // namespace hle::krnl
