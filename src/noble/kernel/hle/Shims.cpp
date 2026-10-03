#include "Shims.h"

#include "Loader/table/ImportTable.h"
#include "Logger.h"
#include "core/byte_swap.h"
#include <cstring>
#include <mutex>

namespace hle {

void* Context::Translate(GuestAddress address, size_t size) const {
    if (!address || !size || size > 0x100000000ull - address)
        throw std::out_of_range("invalid hle guest memory range");

    auto* pointer = memory.Translate(address, size);
    if (!pointer)
        throw std::out_of_range("unmapped hle guest memory range");

    return pointer;
}

uint32_t Context::ReadU32(GuestAddress address) const {
    uint32_t value;

    std::memcpy(&value, Translate(address, sizeof(value)), sizeof(value));
    return byte_swap(value);
}

uint64_t Context::ReadU64(GuestAddress address) const {
    uint64_t value;

    std::memcpy(&value, Translate(address, sizeof(value)), sizeof(value));
    return byte_swap(value);
}

void Context::WriteU32(GuestAddress address, uint32_t value) const {
    value = byte_swap(value);

    std::memcpy(Translate(address, sizeof(value)), &value, sizeof(value));
}

void Context::WriteU64(GuestAddress address, uint64_t value) const {
    value = byte_swap(value);

    std::memcpy(Translate(address, sizeof(value)), &value, sizeof(value));
}

void Registry::RegisterEntry(XboxLibrary library, uint16_t ordinal, EntryPoint entry) {
    const auto* definition = XLoader::FindImport(library, ordinal);

    if (library >= XboxLibrary::Unknown || (definition && definition->type != ImportType::Function))
        throw std::invalid_argument("invalid hle function registration");

    std::unique_lock lock(mutex_);

    entries_.insert_or_assign(Key(library, ordinal), entry);
}

Registry::EntryPoint Registry::Resolve(XboxLibrary library, uint16_t ordinal) const {
    std::shared_lock lock(mutex_);

    const auto it = entries_.find(Key(library, ordinal));
    return it != entries_.end() ? it->second : &MissingImport;
}

void Registry::MissingImport(Registry*, PPCContext* cpu, uint32_t library, uint32_t ordinal,
                             uint32_t thunkAddress) noexcept {
    if (cpu->Fault != PPCFault::None)
        return;

    cpu->CIA = thunkAddress;
    cpu->Fault = PPCFault::UnimplementedImport;
    cpu->FaultAddress = thunkAddress;

    try {
        const char* libraryName = "unknown";
        switch (static_cast<XboxLibrary>(library)) {
        case XboxLibrary::XboxKrnl:
            libraryName = "xboxkrnl.exe";
            break;
        case XboxLibrary::Xam:
            libraryName = "xam.xex";
            break;
        case XboxLibrary::Xbdm:
            libraryName = "xbdm.xex";
            break;
        case XboxLibrary::Xapi:
            libraryName = "xapi.xex";
            break;
        default:
            break;
        }

        const auto* definition
            = XLoader::FindImport(static_cast<XboxLibrary>(library), static_cast<uint16_t>(ordinal));

        LOG_ERROR("Unimplemented HLE import {}!{} ordinal 0x{:04X} at guest thunk 0x{:08X}", libraryName,
                  definition ? definition->name : "unknown", ordinal, thunkAddress);
    } catch (...) {
    }
}

void Registry::ReportFailure(PPCContext& cpu, uint32_t ordinal, uint32_t thunkAddress,
                             const char* message) noexcept {
    cpu.Fault = PPCFault::HLEFailure;
    cpu.FaultAddress = thunkAddress;

    try {
        LOG_ERROR("HLE import 0x{:04X} failed at 0x{:08X}: {}", ordinal, thunkAddress, message);
    } catch (...) {
    }
}

}  // namespace hle
