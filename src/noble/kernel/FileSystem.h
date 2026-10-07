#pragma once

#include "KernelTypes.h"
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

enum class Disposition : uint32_t { Supersede, Open, Create, OpenIf, Overwrite, OverwriteIf };
enum class Action : uint32_t { Superseded, Opened, Created, Overwritten, Exists, DoesNotExist };

struct QueryResult;

// retain a host file or directory and its mount identity for subsequent relative opens
// the stream and guest sharing reservation live until the last reference is released
class File {
public:
    bool IsDirectory() const noexcept { return directory_; }
    bool IsSynchronous() const noexcept { return synchronous_; }

    struct ReadResult {
        XNTSTATUS status;
        uint32_t bytes;
    };
    // read into host memory using an explicit byte offset or the shared file position
    // serialize reads on this handle and advance the file position by the bytes transferred
    ReadResult Read(std::span<uint8_t> buffer, std::optional<uint64_t> offset = {});

    // query handle state and optionally refresh host metadata without changing the file position
    QueryResult Query(bool includeMetadata = true);

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
    uint32_t options_ = 0;
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
    XNTSTATUS status = STATUS_SUCCESS;
    Action action = Action::DoesNotExist;
    std::shared_ptr<File> file;
};

// portable metadata with nt timestamps measured in 100 ns units since 1601
// creation and access times are unavailable and allocation size falls back to logical size
struct FileInformation {
    uint64_t creationTime = 0;
    uint64_t lastAccessTime = 0;
    uint64_t lastWriteTime = 0;
    uint64_t changeTime = 0;
    uint64_t allocationSize = 0;
    uint64_t endOfFile = 0;
    uint32_t attributes = 0;
    uint32_t numberOfLinks = 1;
    uint64_t position = 0;
    uint64_t indexNumber = 0;
    uint32_t access = 0;
    uint32_t mode = 0;
    // counted ansi path relative to the guest mount without a host directory prefix
    std::string name;
};

struct QueryResult {
    XNTSTATUS status = STATUS_SUCCESS;
    FileInformation information;
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

    // query existing file or directory metadata without opening a stream or reserving guest sharing
    QueryResult Query(std::string_view path, const std::shared_ptr<File>& root = {});

private:
    struct ResolvedPath {
        std::filesystem::path root, path;
        bool readOnly = true;
    };
    // caller holds the filesystem mutex while resolving mount mappings
    XNTSTATUS Resolve(std::string_view name, const std::shared_ptr<File>& root, ResolvedPath& resolved);

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
