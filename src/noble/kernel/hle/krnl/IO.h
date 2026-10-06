#pragma once

#include "Rtl.h"
#include "kernel/KernelTypes.h"
#include "kernel/hle/Exports.h"

namespace hle::krnl {

// xbox object attributes use a counted ansi name and have no host nt length or security fields
struct ObjectAttributes {
    be<uint32_t> rootDirectory;
    GuestPointer<const AnsiString> name;
    be<uint32_t> attributes;
};
static_assert(sizeof(ObjectAttributes) == 12);
static_assert(offsetof(ObjectAttributes, attributes) == 8);

// report the final ntstatus and file creation action in guest byte order
struct IOStatusBlock {
    be<XNTSTATUS> status;
    be<uint32_t> information;
};
static_assert(sizeof(IOStatusBlock) == 8);

// file and directory metadata returned in guest byte order
struct FileNetworkOpenInformation {
    be<uint64_t> creationTime;
    be<uint64_t> lastAccessTime;
    be<uint64_t> lastWriteTime;
    be<uint64_t> changeTime;
    be<uint64_t> allocationSize;
    be<uint64_t> endOfFile;
    be<uint32_t> attributes;
    be<uint32_t> padding;
};
static_assert(sizeof(FileNetworkOpenInformation) == 56);
static_assert(offsetof(FileNetworkOpenInformation, attributes) == 48);

// query metadata by counted guest path without creating a file handle
XNTSTATUS
NtQueryFullAttributesFile(Kernel& kernel, KThread& thread, Memory& memory,
                          Pointer<const ObjectAttributes, PointerValidation::Report> attributes,
                          Pointer<FileNetworkOpenInformation, PointerValidation::Report> information);

XNTSTATUS NtCreateFile(Kernel& kernel, KThread& thread, Memory& memory,
                       Pointer<be<uint32_t>, PointerValidation::Report> handle, uint32_t desiredAccess,
                       Pointer<const ObjectAttributes, PointerValidation::Report> attributes,
                       Pointer<IOStatusBlock, PointerValidation::Report> ioStatus,
                       Pointer<const be<int64_t>, PointerValidation::Report> allocationSize,
                       uint32_t fileAttributes, uint32_t shareAccess, uint32_t disposition, uint32_t options);

XNTSTATUS NtReadFile(Kernel& kernel, KThread& thread, Memory& memory, uint32_t fileHandle,
                     uint32_t eventHandle, GuestAddress apcRoutine, GuestAddress apcContext,
                     Pointer<IOStatusBlock, PointerValidation::Report> ioStatus, GuestAddress buffer,
                     uint32_t length, Pointer<const be<int64_t>, PointerValidation::Report> byteOffset);

XNTSTATUS NtClose(KThread& thread, uint32_t handle);

std::span<const Export> IOExports();

}  // namespace hle::krnl
