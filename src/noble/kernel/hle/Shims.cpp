#include "Shims.h"

#include "Loader/table/ImportTable.h"
#include "Logger.h"
#include <cstring>
#include <mutex>

namespace hle {

namespace {

// resolve a variable name against the export catalogue before allocating its guest storage
const XLoader::ImportDefinition& VariableDefinition(XboxLibrary library, std::string_view name) {
    for (const auto& definition : XLoader::importTable)
        if (definition.library == library && definition.name == name
            && definition.type == ImportType::Variable)
            return definition;

    throw std::invalid_argument("unknown hle variable export name");
}

}  // namespace

Registry::~Registry() {
    ClearVariables();
}

GuestAddress Registry::DefineVariableBytes(XboxLibrary library, std::string_view name,
                                           std::span<const std::byte> bytes) {
    const auto& definition = VariableDefinition(library, name);
    if (bytes.empty())
        throw std::invalid_argument("empty hle variable export");

    std::unique_lock lock(mutex_);
    const auto key = Key(library, definition.ordinal);
    if (variables_.contains(key))
        throw std::logic_error("duplicate hle variable export");

    const GuestAddress address = memory_.AllocateVirtual(bytes.size());
    if (!address)
        throw std::runtime_error("unable to allocate hle variable export");

    try {
        std::memcpy(memory_.Translate(address, bytes.size()), bytes.data(), bytes.size());
        variables_.emplace(key, Variable{address, bytes.size()});
    } catch (...) {
        memory_.FreeVirtual(address);
        throw;
    }

    return address;
}

void Registry::UpdateVariableBytes(XboxLibrary library, std::string_view name,
                                   std::span<const std::byte> bytes) {
    const auto& definition = VariableDefinition(library, name);
    std::unique_lock lock(mutex_);

    const auto it = variables_.find(Key(library, definition.ordinal));
    if (it == variables_.end() || it->second.size != bytes.size())
        throw std::invalid_argument("missing hle variable export or mismatched value size");

    std::memcpy(memory_.Translate(it->second.address, bytes.size()), bytes.data(), bytes.size());
}

void Registry::BindVariableImport(const XLoader::Import& import) const {
    if (import.type != ImportType::Variable || import.ordinal > UINT16_MAX || !import.tableAddr)
        throw std::invalid_argument("invalid variable import");

    std::shared_lock lock(mutex_);

    const auto it = variables_.find(Key(import.library, static_cast<uint16_t>(import.ordinal)));
    if (it == variables_.end()) {
        LOG_ERROR("Unimplemented HLE variable import {}!{} ordinal 0x{:04X} at guest slot 0x{:08X}",
                  import.libraryName, import.name, import.ordinal, import.tableAddr);
        throw std::runtime_error("unimplemented hle variable import: " + import.name);
    }

    auto* slot = memory_.Translate(import.tableAddr, sizeof(uint32_t));
    if (!slot)
        throw std::runtime_error("unmapped variable import slot");

    const be<uint32_t> encoded(it->second.address);
    std::memcpy(slot, &encoded, sizeof(encoded));
}

void Registry::ClearVariables() {
    std::unique_lock lock(mutex_);
    for (const auto& [key, variable] : variables_)
        memory_.FreeVirtual(variable.address);
    variables_.clear();
    for (const auto address : variableStorage_)
        memory_.FreeVirtual(address);
    variableStorage_.clear();
}

GuestAddress Registry::AllocateVariableStorage(size_t size) {
    std::unique_lock lock(mutex_);
    const GuestAddress address = memory_.AllocateVirtual(size);
    if (!address)
        throw std::runtime_error("unable to allocate hle variable backing storage");

    try {
        variableStorage_.push_back(address);
    } catch (...) {
        memory_.FreeVirtual(address);
        throw;
    }
    return address;
}

GuestAddress Registry::VariableAddress(XboxLibrary library, std::string_view name) const {
    const auto& definition = VariableDefinition(library, name);
    std::shared_lock lock(mutex_);
    const auto it = variables_.find(Key(library, definition.ordinal));
    if (it == variables_.end())
        throw std::invalid_argument("missing hle variable export");
    return it->second.address;
}

void* Context::Translate(GuestAddress address, size_t size) const {
    if (!address || !size || size > 0x100000000ull - address)
        throw std::out_of_range("invalid hle guest memory range");

    auto* pointer = memory.Translate(address, size);
    if (!pointer)
        throw std::out_of_range("unmapped hle guest memory range");

    return pointer;
}

uint32_t Context::ReadU32(GuestAddress address) const {
    be<uint32_t> value;

    std::memcpy(&value, Translate(address, sizeof(value)), sizeof(value));
    return value;
}

uint64_t Context::ReadU64(GuestAddress address) const {
    be<uint64_t> value;

    std::memcpy(&value, Translate(address, sizeof(value)), sizeof(value));
    return value;
}

void Context::WriteU32(GuestAddress address, uint32_t value) const {
    if (!memory.IsAccessible(address, sizeof(value), true))
        throw std::out_of_range("hle write to inaccessible guest memory");
    const be<uint32_t> encoded(value);
    std::memcpy(Translate(address, sizeof(encoded)), &encoded, sizeof(encoded));
}

void Context::WriteU64(GuestAddress address, uint64_t value) const {
    if (!memory.IsAccessible(address, sizeof(value), true))
        throw std::out_of_range("hle write to inaccessible guest memory");
    const be<uint64_t> encoded(value);
    std::memcpy(Translate(address, sizeof(encoded)), &encoded, sizeof(encoded));
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
