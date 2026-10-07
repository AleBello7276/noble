#include "IO.h"

#include "kernel/KFile.h"
#include "kernel/Kernel.h"
#include <array>
#include <algorithm>
#include <cstring>

namespace hle::krnl {

XNTSTATUS ReadObjectPath(KThread& thread, Memory& memory, const ObjectAttributes& attributes,
                         std::string& path, std::shared_ptr<vfs::File>& root) {
    if (!thread.process())
        return X_STATUS_INVALID_HANDLE;

    // ordinary object flags and case insensitive names need no special host behavior
    if (uint32_t(attributes.attributes) & ~(0x2u | 0x40u))
        return X_STATUS_NOT_SUPPORTED;

    Pointer<const AnsiString, PointerValidation::Report> name(memory, attributes.name);
    if (!name)
        return name.guest_address() ? X_STATUS_ACCESS_VIOLATION : X_STATUS_INVALID_PARAMETER;

    const uint16_t length = name->length;
    if (!length)
        return X_STATUS_OBJECT_NAME_INVALID;

    if (!name->buffer)
        return X_STATUS_ACCESS_VIOLATION;

    const auto* bytes = static_cast<const char*>(memory.Translate(name->buffer, length));
    if (!bytes)
        return X_STATUS_ACCESS_VIOLATION;

    // retain the counted path and directory before output writes or host operations
    path.assign(bytes, length);
    const uint32_t rootHandle = attributes.rootDirectory;
    if (rootHandle && rootHandle != 0xFFFFFFFD) {
        auto object = thread.process()->handles.Lookup(rootHandle);
        if (!object)
            return X_STATUS_INVALID_HANDLE;

        if (object->type() != KernelObjectType::KFile)
            return X_STATUS_OBJECT_TYPE_MISMATCH;

        root = std::static_pointer_cast<KFile>(object)->file();
    }

    return STATUS_SUCCESS;
}

XNTSTATUS NtCreateFile(Kernel& kernel, KThread& thread, Memory& memory,
                       Pointer<be<uint32_t>, PointerValidation::Report> handle, uint32_t desiredAccess,
                       Pointer<const ObjectAttributes, PointerValidation::Report> attributes,
                       Pointer<IOStatusBlock, PointerValidation::Report> ioStatus,
                       Pointer<const be<int64_t>, PointerValidation::Report> allocationSize,
                       uint32_t fileAttributes, uint32_t shareAccess, uint32_t disposition,
                       uint32_t options) {
    const auto complete = [&](XNTSTATUS status, vfs::Action action = vfs::Action::DoesNotExist) {
        if (ioStatus) {
            ioStatus->status = status;
            ioStatus->information = uint32_t(action);
        }

        return status;
    };

    if (!handle)
        return complete(handle.guest_address() ? X_STATUS_ACCESS_VIOLATION : X_STATUS_INVALID_PARAMETER);

    *handle = UINT32_MAX;
    if (ioStatus.guest_address() && !ioStatus)
        return X_STATUS_ACCESS_VIOLATION;

    if (!attributes)
        return complete(attributes.guest_address() ? X_STATUS_ACCESS_VIOLATION : X_STATUS_INVALID_PARAMETER);

    if (allocationSize.guest_address() && !allocationSize)
        return complete(X_STATUS_ACCESS_VIOLATION);

    if (allocationSize && int64_t(*allocationSize) < 0)
        return complete(X_STATUS_INVALID_PARAMETER);

    try {
        std::string path;
        std::shared_ptr<vfs::File> root;
        const auto status = ReadObjectPath(thread, memory, *attributes, path, root);
        if (status != STATUS_SUCCESS)
            return complete(status);

        const auto result = kernel.Files().Open(
            {path, root, desiredAccess, shareAccess, static_cast<vfs::Disposition>(disposition), options,
             fileAttributes, allocationSize ? uint64_t(int64_t(*allocationSize)) : 0});

        if (result.status != STATUS_SUCCESS)
            return complete(result.status, result.action);

        auto file = std::make_shared<KFile>(result.file);
        *handle = thread.process()->handles.Insert(std::move(file));

        return complete(STATUS_SUCCESS, result.action);
    } catch (const std::bad_alloc&) {
        return complete(X_STATUS_INSUFFICIENT_RESOURCES);
    } catch (const std::filesystem::filesystem_error&) {
        return complete(X_STATUS_IO_ERROR);
    }
}

XNTSTATUS
NtQueryFullAttributesFile(Kernel& kernel, KThread& thread, Memory& memory,
                          Pointer<const ObjectAttributes, PointerValidation::Report> attributes,
                          Pointer<FileNetworkOpenInformation, PointerValidation::Report> information) {
    if (!attributes)
        return attributes.guest_address() ? X_STATUS_ACCESS_VIOLATION : X_STATUS_INVALID_PARAMETER;

    if (!information)
        return information.guest_address() ? X_STATUS_ACCESS_VIOLATION : X_STATUS_INVALID_PARAMETER;

    try {
        std::string path;
        std::shared_ptr<vfs::File> root;
        const auto status = ReadObjectPath(thread, memory, *attributes, path, root);
        if (status != STATUS_SUCCESS)
            return status;

        const auto result = kernel.Files().Query(path, root);
        if (result.status != STATUS_SUCCESS)
            return result.status;

        const auto& info = result.information;
        // publish the whole guest record only on success and clear the trailing padding
        *information = {info.creationTime,   info.lastAccessTime, info.lastWriteTime, info.changeTime,
                        info.allocationSize, info.endOfFile,      info.attributes,    0};
        return STATUS_SUCCESS;
    } catch (const std::bad_alloc&) {
        return X_STATUS_INSUFFICIENT_RESOURCES;
    } catch (const std::filesystem::filesystem_error&) {
        return X_STATUS_IO_ERROR;
    }
}

XNTSTATUS NtReadFile(Kernel& kernel, KThread& thread, Memory& memory, uint32_t fileHandle,
                     uint32_t eventHandle, GuestAddress apcRoutine, [[maybe_unused]] GuestAddress apcContext,
                     Pointer<IOStatusBlock, PointerValidation::Report> ioStatus, GuestAddress buffer,
                     uint32_t length, Pointer<const be<int64_t>, PointerValidation::Report> byteOffset) {
    const auto complete = [&](XNTSTATUS status, uint32_t bytes = 0) {
        if (ioStatus) {
            ioStatus->status = status;
            ioStatus->information = bytes;
        }

        return status;
    };

    if (ioStatus.guest_address() && !ioStatus)
        return X_STATUS_ACCESS_VIOLATION;

    if (byteOffset.guest_address() && !byteOffset)
        return complete(X_STATUS_ACCESS_VIOLATION);

    if (!thread.process())
        return complete(X_STATUS_INVALID_HANDLE);

    auto object = thread.process()->handles.Lookup(fileHandle);
    if (!object || object->type() != KernelObjectType::KFile)
        return complete(X_STATUS_INVALID_HANDLE);

    const auto file = std::static_pointer_cast<KFile>(object)->file();
    if (apcRoutine & ~1u)
        return complete(X_STATUS_NOT_SUPPORTED);

    std::optional<uint64_t> offset;
    if (byteOffset) {
        const int64_t value = *byteOffset;

        if (value >= 0)
            offset = uint64_t(value);
        else if ((value != -1 && value != -2) || !file->IsSynchronous())
            return complete(X_STATUS_INVALID_PARAMETER);

    } else if (!file->IsSynchronous()) {
        return complete(X_STATUS_INVALID_PARAMETER);
    }

    if (length && (!buffer || !memory.IsAccessible(buffer, length, true)))
        return complete(X_STATUS_ACCESS_VIOLATION);

    auto* destination = length ? static_cast<uint8_t*>(memory.Translate(buffer, length)) : nullptr;
    if (length && !destination)
        return complete(X_STATUS_ACCESS_VIOLATION);

    std::shared_ptr<KernelObject> event;
    if (eventHandle) {
        event = thread.process()->handles.Lookup(eventHandle);

        if (!event || event->type() != KernelObjectType::KEvent)
            return complete(X_STATUS_INVALID_HANDLE);

        const auto address = event->guest_address();
        if (!address || address % 4 || !memory.IsAccessible(address, sizeof(X_DISPATCH_HEADER), true))
            return complete(X_STATUS_INVALID_HANDLE);

        const auto* header
            = static_cast<const X_DISPATCH_HEADER*>(memory.Translate(address, sizeof(X_DISPATCH_HEADER)));

        if (!header
            || (header->type != EventNotificationObject && header->type != EventSynchronizationObject))
            return complete(X_STATUS_INVALID_HANDLE);

        kernel.ResetEvent(address);
    }

    const auto result = file->Read({destination, length}, offset);
    const auto status = complete(result.status, result.bytes);
    // publish output status before releasing an event waiter
    if (event)
        kernel.SetEvent(event->guest_address());

    return status;
}

XNTSTATUS NtQueryInformationFile(KThread& thread, Memory& memory, uint32_t fileHandle,
                                 Pointer<IOStatusBlock, PointerValidation::Report> ioStatus,
                                 GuestAddress information, uint32_t length, uint32_t informationClass) {
    const auto complete = [&](XNTSTATUS status, uint32_t bytes = 0) {
        if (ioStatus) {
            ioStatus->status = status;
            ioStatus->information = bytes;
        }
        return status;
    };
    if (ioStatus.guest_address() && !ioStatus)
        return X_STATUS_ACCESS_VIOLATION;

    const auto query = static_cast<FileInformationClass>(informationClass);
    uint32_t minimum = 0;
    switch (query) {
    case FileInformationClass::Basic:
        minimum = sizeof(FileBasicInformation);
        break;
    case FileInformationClass::Standard:
        minimum = sizeof(FileStandardInformation);
        break;
    case FileInformationClass::Name:
        minimum = sizeof(FileNameInformation);
        break;
    case FileInformationClass::All:
        minimum = sizeof(FileAllInformation);
        break;
    case FileInformationClass::NetworkOpen:
        minimum = sizeof(FileNetworkOpenInformation);
        break;
    case FileInformationClass::Internal:
    case FileInformationClass::Position:
    case FileInformationClass::Allocation:
    case FileInformationClass::EndOfFile:
    case FileInformationClass::AttributeTag:
        minimum = 8;
        break;
    case FileInformationClass::Ea:
    case FileInformationClass::Access:
    case FileInformationClass::Mode:
    case FileInformationClass::Alignment:
        minimum = 4;
        break;
    default:
        return complete(informationClass > 0 && informationClass < 37 ? X_STATUS_NOT_SUPPORTED :
                                                                        X_STATUS_INVALID_INFO_CLASS);
    }
    if (length < minimum)
        return complete(X_STATUS_INFO_LENGTH_MISMATCH);
    if (!thread.process())
        return complete(X_STATUS_INVALID_HANDLE);
    const auto object = thread.process()->handles.Lookup(fileHandle);
    if (!object || object->type() != KernelObjectType::KFile)
        return complete(X_STATUS_INVALID_HANDLE);
    if (!information || !memory.IsAccessible(information, minimum, true))
        return complete(X_STATUS_ACCESS_VIOLATION);

    try {
        const auto file = std::static_pointer_cast<KFile>(object)->file();
        const bool includeMetadata
            = query == FileInformationClass::Basic || query == FileInformationClass::Standard
              || query == FileInformationClass::All || query == FileInformationClass::NetworkOpen
              || query == FileInformationClass::Allocation || query == FileInformationClass::EndOfFile
              || query == FileInformationClass::AttributeTag;
        const auto result = file->Query(includeMetadata);
        if (result.status != STATUS_SUCCESS)
            return complete(result.status);
        const auto& info = result.information;
        const FileBasicInformation basic{info.creationTime, info.lastAccessTime, info.lastWriteTime,
                                         info.changeTime,   info.attributes,     0};
        const FileStandardInformation standard{
            info.allocationSize, info.endOfFile, info.numberOfLinks, 0, uint8_t(file->IsDirectory()), {}};

        const auto publish = [&]<typename T>(const T& value) {
            auto* output = memory.Translate(information, sizeof(T));
            if (!output)
                return complete(X_STATUS_ACCESS_VIOLATION);
            std::memcpy(output, &value, sizeof(T));
            return complete(STATUS_SUCCESS, sizeof(T));
        };

        // retain the full name length even when only a prefix fits in the caller's buffer
        const auto publishName = [&](const auto& header, uint32_t nameOffset) {
            const uint32_t count
                = static_cast<uint32_t>((std::min)(size_t(length - nameOffset), info.name.size()));
            const uint32_t bytes = nameOffset + count;
            if (!memory.IsAccessible(information, bytes, true))
                return complete(X_STATUS_ACCESS_VIOLATION);
            auto* output = static_cast<uint8_t*>(memory.Translate(information, bytes));
            if (!output)
                return complete(X_STATUS_ACCESS_VIOLATION);
            std::memcpy(output, &header, nameOffset);
            std::memcpy(output + nameOffset, info.name.data(), count);
            return complete(count == info.name.size() ? STATUS_SUCCESS : X_STATUS_BUFFER_OVERFLOW, bytes);
        };

        switch (query) {
        case FileInformationClass::Basic:
            return publish(basic);
        case FileInformationClass::Standard:
            return publish(standard);
        case FileInformationClass::Internal:
            return publish(be<uint64_t>(info.indexNumber));
        case FileInformationClass::Ea:
            return publish(be<uint32_t>(0));
        case FileInformationClass::Access:
            return publish(be<uint32_t>(info.access));
        case FileInformationClass::Position:
            return publish(be<uint64_t>(info.position));
        case FileInformationClass::Mode:
            return publish(be<uint32_t>(info.mode));
        case FileInformationClass::Alignment:
            return publish(be<uint32_t>(0));
        case FileInformationClass::Allocation:
            return publish(be<uint64_t>(info.allocationSize));
        case FileInformationClass::EndOfFile:
            return publish(be<uint64_t>(info.endOfFile));
        case FileInformationClass::Name: {
            const FileNameInformation name{uint32_t(info.name.size()), {}, {}};
            return publishName(name, offsetof(FileNameInformation, fileName));
        }
        case FileInformationClass::All: {
            const FileAllInformation all{basic,     standard,    info.indexNumber,
                                         0,         info.access, info.position,
                                         info.mode, 0,           {uint32_t(info.name.size()), {}, {}}};
            return publishName(all,
                               offsetof(FileAllInformation, name) + offsetof(FileNameInformation, fileName));
        }
        case FileInformationClass::NetworkOpen:
            return publish(FileNetworkOpenInformation{
                info.creationTime, info.lastAccessTime, info.lastWriteTime, info.changeTime,
                info.allocationSize, info.endOfFile, info.attributes, 0});
        case FileInformationClass::AttributeTag: {
            const std::array value{be<uint32_t>(info.attributes), be<uint32_t>(0)};
            return publish(value);
        }
        default:
            return complete(X_STATUS_INVALID_INFO_CLASS);
        }
    } catch (const std::bad_alloc&) {
        return complete(X_STATUS_INSUFFICIENT_RESOURCES);
    } catch (const std::filesystem::filesystem_error&) {
        return complete(X_STATUS_IO_ERROR);
    }
}

XNTSTATUS NtClose(KThread& thread, uint32_t handle) {
    return thread.process() && thread.process()->handles.Remove(handle) ? STATUS_SUCCESS :
                                                                          X_STATUS_INVALID_HANDLE;
}

constexpr std::array exports{
    Bind<&NtCreateFile>(XboxLibrary::XboxKrnl, "NtCreateFile"),
    Bind<&NtReadFile>(XboxLibrary::XboxKrnl, "NtReadFile"),
    Bind<&NtQueryFullAttributesFile>(XboxLibrary::XboxKrnl, "NtQueryFullAttributesFile"),
    Bind<&NtQueryInformationFile>(XboxLibrary::XboxKrnl, "NtQueryInformationFile"),
    Bind<&NtClose>(XboxLibrary::XboxKrnl, "NtClose"),
};

std::span<const Export> IOExports() {
    return exports;
}

}  // namespace hle::krnl
