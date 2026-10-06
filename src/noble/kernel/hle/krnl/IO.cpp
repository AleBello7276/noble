#include "IO.h"

#include "kernel/KFile.h"
#include "kernel/Kernel.h"
#include <array>

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

XNTSTATUS NtClose(KThread& thread, uint32_t handle) {
    return thread.process() && thread.process()->handles.Remove(handle) ? STATUS_SUCCESS :
                                                                          X_STATUS_INVALID_HANDLE;
}

constexpr std::array exports{
    Bind<&NtCreateFile>(XboxLibrary::XboxKrnl, "NtCreateFile"),
    Bind<&NtReadFile>(XboxLibrary::XboxKrnl, "NtReadFile"),
    Bind<&NtQueryFullAttributesFile>(XboxLibrary::XboxKrnl, "NtQueryFullAttributesFile"),
    Bind<&NtClose>(XboxLibrary::XboxKrnl, "NtClose"),
};

std::span<const Export> IOExports() {
    return exports;
}

}  // namespace hle::krnl
