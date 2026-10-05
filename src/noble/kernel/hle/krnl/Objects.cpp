#include "Objects.h"

#include "Logger.h"
#include "kernel/Kernel.h"
#include <array>

namespace hle::krnl {

uint32_t ObReferenceObjectByHandle(Kernel& kernel, KThread& caller, uint32_t handle, GuestAddress object_type,
                                   Pointer<be<uint32_t>, PointerValidation::Report> out_object) {
    if (!out_object)
        return out_object.guest_address() ? 0xC0000005 : 0xC000000D;

    *out_object = 0;

    KernelObject* object = nullptr;
    std::shared_ptr<KernelObject> owned;

    if (handle == 0xFFFFFFFE) {
        object = &caller;
    } else if (caller.process() && handle != 0xFFFFFFFF) {
        owned = caller.process()->handles.Lookup(handle);
        object = owned.get();
    }

    if (!object)
        return X_STATUS_INVALID_HANDLE;

    if (object_type) {
        const auto expected
            = object->type() == KernelObjectType::KThread ?
                  kernel.Imports().VariableAddress(XboxLibrary::XboxKrnl, "ExThreadObjectType") :
                  0;

        if (!expected || object_type != expected)
            return X_STATUS_OBJECT_TYPE_MISMATCH;
    }

    const GuestAddress address = object->guest_address();

    if (!address) {
        return X_STATUS_INVALID_HANDLE;
    }

    if (!object->RetainGuestReference()) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    *out_object = address;
    return STATUS_SUCCESS;
}

void ObReferenceObject(Kernel& kernel, GuestAddress address) {
    auto* object = kernel.LookupGuestObject(address);

    if (!object || !object->RetainGuestReference()) {
        LOG_WARN("ObReferenceObject received an invalid object 0x{:08X}", address);
    }
}

void ObDereferenceObject(Kernel& kernel, GuestAddress address) {
    auto* object = kernel.LookupGuestObject(address);

    if (!object || !object->ReleaseGuestReference()) {
        LOG_WARN("ObDereferenceObject received an invalid or unreferenced object 0x{:08X}", address);
    }
}

constexpr std::array exports{
    Bind<&ObReferenceObjectByHandle>(XboxLibrary::XboxKrnl, "ObReferenceObjectByHandle"),
    Bind<&ObReferenceObject>(XboxLibrary::XboxKrnl, "ObReferenceObject"),
    Bind<&ObDereferenceObject>(XboxLibrary::XboxKrnl, "ObDereferenceObject"),
};

std::span<const Export> ObjectExports() {
    return exports;
}

}  // namespace hle::krnl
