#include "IO.h"

#include "kernel/KFile.h"
#include "kernel/Kernel.h"
#include <array>

namespace hle::krnl {

uint32_t NtCreateFile(Kernel& kernel, KThread& thread, Memory& memory,
                      Pointer<be<uint32_t>, PointerValidation::Report> handle, uint32_t desiredAccess,
                      Pointer<const ObjectAttributes, PointerValidation::Report> attributes,
                      Pointer<IOStatusBlock, PointerValidation::Report> ioStatus,
                      Pointer<const be<int64_t>, PointerValidation::Report> allocationSize,
                      uint32_t fileAttributes, uint32_t shareAccess, uint32_t disposition, uint32_t options) {
    using vfs::Status;
    const auto complete = [&](Status status, vfs::Action action = vfs::Action::DoesNotExist) {
        if (ioStatus) {
            ioStatus->status = uint32_t(status);
            ioStatus->information = uint32_t(action);
        }

        return uint32_t(status);
    };

    if (!handle)
        return complete(handle.guest_address() ? Status::AccessViolation : Status::InvalidParameter);

    *handle = UINT32_MAX;
    if (ioStatus.guest_address() && !ioStatus)
        return uint32_t(Status::AccessViolation);

    if (!attributes)
        return complete(attributes.guest_address() ? Status::AccessViolation : Status::InvalidParameter);

    if (allocationSize.guest_address() && !allocationSize)
        return complete(Status::AccessViolation);

    if (allocationSize && int64_t(*allocationSize) < 0)
        return complete(Status::InvalidParameter);

    if (!thread.process())
        return complete(Status::InvalidHandle);

    // case insensitive names and ordinary object handle flags require no special host behavior
    if (uint32_t(attributes->attributes) & ~(0x2u | 0x40u))
        return complete(Status::NotSupported);

    Pointer<const AnsiString, PointerValidation::Report> name(memory, attributes->name);
    if (!name)
        return complete(name.guest_address() ? Status::AccessViolation : Status::InvalidParameter);

    const uint16_t length = name->length;
    if (!length)
        return complete(Status::ObjectNameInvalid);

    const auto* bytes = static_cast<const char*>(memory.Translate(name->buffer, length));
    if (!bytes)
        return complete(Status::AccessViolation);

    try {
        // copy the counted path before any output or host filesystem operation changes guest memory
        const std::string path(bytes, length);
        std::shared_ptr<vfs::File> root;
        const uint32_t rootHandle = attributes->rootDirectory;

        if (rootHandle && rootHandle != 0xFFFFFFFD) {
            auto object = thread.process()->handles.Lookup(rootHandle);
            if (!object)
                return complete(Status::InvalidHandle);

            if (object->type() != KernelObjectType::KFile)
                return complete(Status::ObjectTypeMismatch);

            root = std::static_pointer_cast<KFile>(object)->file();
        }

        const auto result = kernel.Files().Open(
            {path, root, desiredAccess, shareAccess, static_cast<vfs::Disposition>(disposition), options,
             fileAttributes, allocationSize ? uint64_t(int64_t(*allocationSize)) : 0});

        if (result.status != Status::Success)
            return complete(result.status, result.action);

        auto file = std::make_shared<KFile>(result.file);
        *handle = thread.process()->handles.Insert(std::move(file));

        return complete(Status::Success, result.action);
    } catch (const std::bad_alloc&) {
        return complete(Status::InsufficientResources);
    } catch (const std::filesystem::filesystem_error&) {
        return complete(Status::IOError);
    }
}

uint32_t NtReadFile(Kernel& kernel, KThread& thread, Memory& memory, uint32_t fileHandle,
                    uint32_t eventHandle, GuestAddress apcRoutine, [[maybe_unused]] GuestAddress apcContext,
                    Pointer<IOStatusBlock, PointerValidation::Report> ioStatus, GuestAddress buffer,
                    uint32_t length, Pointer<const be<int64_t>, PointerValidation::Report> byteOffset) {
    using vfs::Status;
    const auto complete = [&](Status status, uint32_t bytes = 0) {
        if (ioStatus) {
            ioStatus->status = uint32_t(status);
            ioStatus->information = bytes;
        }

        return uint32_t(status);
    };

    if (ioStatus.guest_address() && !ioStatus)
        return uint32_t(Status::AccessViolation);

    if (byteOffset.guest_address() && !byteOffset)
        return complete(Status::AccessViolation);

    if (!thread.process())
        return complete(Status::InvalidHandle);

    auto object = thread.process()->handles.Lookup(fileHandle);
    if (!object || object->type() != KernelObjectType::KFile)
        return complete(Status::InvalidHandle);

    const auto file = std::static_pointer_cast<KFile>(object)->file();
    if (apcRoutine & ~1u)
        return complete(Status::NotSupported);

    std::optional<uint64_t> offset;
    if (byteOffset) {
        const int64_t value = *byteOffset;

        if (value >= 0)
            offset = uint64_t(value);
        else if ((value != -1 && value != -2) || !file->IsSynchronous())
            return complete(Status::InvalidParameter);

    } else if (!file->IsSynchronous()) {
        return complete(Status::InvalidParameter);
    }

    if (length && (!buffer || !memory.IsAccessible(buffer, length, true)))
        return complete(Status::AccessViolation);

    auto* destination = length ? static_cast<uint8_t*>(memory.Translate(buffer, length)) : nullptr;
    if (length && !destination)
        return complete(Status::AccessViolation);

    std::shared_ptr<KernelObject> event;
    if (eventHandle) {
        event = thread.process()->handles.Lookup(eventHandle);

        if (!event || event->type() != KernelObjectType::KEvent)
            return complete(Status::InvalidHandle);

        const auto address = event->guest_address();
        if (!address || address % 4 || !memory.IsAccessible(address, sizeof(X_DISPATCH_HEADER), true))
            return complete(Status::InvalidHandle);

        const auto* header
            = static_cast<const X_DISPATCH_HEADER*>(memory.Translate(address, sizeof(X_DISPATCH_HEADER)));

        if (!header
            || (header->type != EventNotificationObject && header->type != EventSynchronizationObject))
            return complete(Status::InvalidHandle);

        kernel.ResetEvent(address);
    }

    const auto result = file->Read({destination, length}, offset);
    const auto status = complete(result.status, result.bytes);
    // publish output status before releasing an event waiter
    if (event)
        kernel.SetEvent(event->guest_address());

    return status;
}

uint32_t NtClose(KThread& thread, uint32_t handle) {
    return uint32_t(thread.process() && thread.process()->handles.Remove(handle) ?
                        vfs::Status::Success :
                        vfs::Status::InvalidHandle);
}

constexpr std::array exports{
    Bind<&NtCreateFile>(XboxLibrary::XboxKrnl, "NtCreateFile"),
    Bind<&NtReadFile>(XboxLibrary::XboxKrnl, "NtReadFile"),
    Bind<&NtClose>(XboxLibrary::XboxKrnl, "NtClose"),
};

std::span<const Export> IOExports() {
    return exports;
}

}  // namespace hle::krnl
