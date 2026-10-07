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

// xbox file query classes retain nt numbering and use counted ansi filenames
enum class FileInformationClass : uint32_t {
    Basic = 4,
    Standard = 5,
    Internal = 6,
    Ea = 7,
    Access = 8,
    Name = 9,
    Position = 14,
    Mode = 16,
    Alignment = 17,
    All = 18,
    Allocation = 19,
    EndOfFile = 20,
    NetworkOpen = 34,
    AttributeTag = 35,
};

// timestamps and attributes with explicit guest padding
struct FileBasicInformation {
    be<uint64_t> creationTime, lastAccessTime, lastWriteTime, changeTime;
    be<uint32_t> attributes, padding;
};
static_assert(sizeof(FileBasicInformation) == 40);

// logical and allocated sizes plus link count and one byte boolean flags
struct FileStandardInformation {
    be<uint64_t> allocationSize, endOfFile;
    be<uint32_t> numberOfLinks;
    uint8_t deletePending, directory;
    uint8_t padding[2];
};
static_assert(sizeof(FileStandardInformation) == 24);
static_assert(offsetof(FileStandardInformation, directory) == 21);

// file names are not terminated and may extend beyond the initial name byte
struct FileNameInformation {
    be<uint32_t> fileNameLength;
    char fileName[1];
    uint8_t padding[3];
};
static_assert(sizeof(FileNameInformation) == 8);
static_assert(offsetof(FileNameInformation, fileName) == 4);

// fixed records followed by the same variable length ansi filename
struct FileAllInformation {
    FileBasicInformation basic;
    FileStandardInformation standard;
    be<uint64_t> indexNumber;
    be<uint32_t> eaSize, access;
    be<uint64_t> currentByteOffset;
    be<uint32_t> mode, alignmentRequirement;
    FileNameInformation name;
};
static_assert(sizeof(FileAllInformation) == 104);
static_assert(offsetof(FileAllInformation, name) == 96);

// query an open guest file and report the number of output bytes in the io status block
XNTSTATUS NtQueryInformationFile(KThread& thread, Memory& memory, uint32_t fileHandle,
                                 Pointer<IOStatusBlock, PointerValidation::Report> ioStatus,
                                 GuestAddress information, uint32_t length, uint32_t informationClass);

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
