#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace vfs {

enum class Status : uint32_t {
    Success = 0,
    AccessViolation = 0xC0000005,
    InvalidHandle = 0xC0000008,
    InvalidDeviceRequest = 0xC0000010,
    EndOfFile = 0xC0000011,
    InvalidParameter = 0xC000000D,
    AccessDenied = 0xC0000022,
    ObjectTypeMismatch = 0xC0000024,
    ObjectNameInvalid = 0xC0000033,
    ObjectNameNotFound = 0xC0000034,
    ObjectNameCollision = 0xC0000035,
    ObjectPathNotFound = 0xC000003A,
    SharingViolation = 0xC0000043,
    InsufficientResources = 0xC000009A,
    MediaWriteProtected = 0xC00000A2,
    FileIsADirectory = 0xC00000BA,
    NotSupported = 0xC00000BB,
    NotADirectory = 0xC0000103,
    IOError = 0xC0000185,
};

enum class Disposition : uint32_t { Supersede, Open, Create, OpenIf, Overwrite, OverwriteIf };
enum class Action : uint32_t { Superseded, Opened, Created, Overwritten, Exists, DoesNotExist };

// retain a host file or directory and its mount identity for subsequent relative opens
// the stream and guest sharing reservation live until the last reference is released
class File {
public:
    bool IsDirectory() const noexcept { return directory_; }
    bool IsSynchronous() const noexcept { return synchronous_; }
    struct ReadResult {
        Status status;
        uint32_t bytes;
    };
    // read into host memory using an explicit byte offset or the shared file position
    // serialize reads on this handle and advance the file position by the bytes transferred
    ReadResult Read(std::span<uint8_t> buffer, std::optional<uint64_t> offset = {});

private:
    friend class FileSystem;
    std::filesystem::path root_, path_;
    std::fstream stream_;
    std::mutex ioMutex_;
    uint64_t position_ = 0;
    bool directory_ = false;
    bool synchronous_ = false;
    bool readOnly_ = true;
    uint32_t access_ = 0;
    uint32_t sharing_ = 0;
};

struct OpenRequest {
    std::string_view path;
    std::shared_ptr<File> root;
    uint32_t desiredAccess = 0;
    uint32_t shareAccess = 0;
    Disposition disposition = Disposition::Open;
    uint32_t options = 0;
    // accepted for xbox api compatibility without persisting nt file attribute metadata
    uint32_t attributes = 0;
    // reservation hint without changing the logical end of file or guaranteeing preallocation
    uint64_t allocationSize = 0;
};

struct OpenResult {
    Status status = Status::Success;
    Action action = Action::DoesNotExist;
    std::shared_ptr<File> file;
};

// translate mounted xbox paths and enforce guest sharing without exposing host paths to hle code
// standard library host operations are confined to this service and can be replaced by a backend
class FileSystem {
public:
    // mount an existing host directory under an xbox drive or device prefix
    // mount paths are case insensitive and writable access must be enabled explicitly
    bool Mount(std::string_view prefix, const std::filesystem::path& directory, bool readOnly = true);

    // discard mount mappings after process handles have been released
    void Clear();

    // open or create a file and report the guest disposition action and ntstatus
    // guest sharing is enforced inside this service and does not impose sharing locks on other host programs
    // symlink traversal and superseding a file with live handles are unsupported by this backend
    OpenResult Open(const OpenRequest& request);

private:
    struct MountPoint {
        std::string prefix;
        std::filesystem::path root;
        bool readOnly;
    };
    std::mutex mutex_;
    std::vector<MountPoint> mounts_;
    std::vector<std::weak_ptr<File>> opened_;
};

}  // namespace vfs
