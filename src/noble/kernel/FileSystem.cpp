#include "FileSystem.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <limits>

namespace vfs {

std::string Fold(std::string_view value) {
    std::string result(value);

    for (auto& c : result) {
        if (c == '/')
            c = '\\';

        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
    }

    return result;
}

XNTSTATUS HostError(const std::error_code& error) {
    if (error == std::errc::permission_denied)
        return X_STATUS_ACCESS_DENIED;

    if (error == std::errc::no_such_file_or_directory)
        return X_STATUS_OBJECT_PATH_NOT_FOUND;

    if (error == std::errc::file_exists)
        return X_STATUS_OBJECT_NAME_COLLISION;

    if (error == std::errc::not_a_directory)
        return X_STATUS_NOT_A_DIRECTORY;

    if (error == std::errc::read_only_file_system)
        return X_STATUS_MEDIA_WRITE_PROTECTED;

    if (error == std::errc::too_many_files_open || error == std::errc::not_enough_memory)
        return X_STATUS_INSUFFICIENT_RESOURCES;

    return X_STATUS_IO_ERROR;
}

uint32_t ExpandAccess(uint32_t access, bool readOnly) {
    if (access & 0x80000000)
        access |= 0x120089;

    if (access & 0x40000000)
        access |= 0x120116;

    if (access & 0x20000000)
        access |= 0x1200A0;

    if (access & 0x10000000)
        access |= 0x1F01FF;

    if (access & 0x02000000)
        access |= readOnly ? 0x1200A9 : 0x1F01FF;

    return access & ~0xF2000000u;
}

uint32_t SharingAccess(uint32_t access) {
    return ((access & 0x21) ? 1u : 0u) | ((access & 6) ? 2u : 0u) | ((access & 0x10000) ? 4u : 0u);
}

File::ReadResult File::Read(std::span<uint8_t> buffer, std::optional<uint64_t> offset) {
    std::scoped_lock lock(ioMutex_);

    if (directory_)
        return {X_STATUS_INVALID_DEVICE_REQUEST, 0};

    if (!(access_ & 1))
        return {X_STATUS_ACCESS_DENIED, 0};

    if (buffer.empty())
        return {STATUS_SUCCESS, 0};

    const auto position = offset.value_or(position_);
    if (position > uint64_t((std::numeric_limits<std::streamoff>::max)())
        || buffer.size() > uint64_t((std::numeric_limits<std::streamsize>::max)())
        || buffer.size() > UINT32_MAX)
        return {X_STATUS_INVALID_PARAMETER, 0};

    stream_.clear();
    stream_.seekg(static_cast<std::streamoff>(position), std::ios::beg);
    if (!stream_)
        return {X_STATUS_IO_ERROR, 0};

    stream_.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
    const auto count = static_cast<uint32_t>(stream_.gcount());
    if (stream_.bad() || (stream_.fail() && !stream_.eof()))
        return {X_STATUS_IO_ERROR, count};

    if (!count && stream_.eof())
        return {X_STATUS_END_OF_FILE, 0};

    position_ = position + count;

    return {STATUS_SUCCESS, count};
}

bool FileSystem::Mount(std::string_view prefix, const std::filesystem::path& directory, bool readOnly) {
    if (prefix.empty())
        return false;

    std::error_code error;
    auto root = std::filesystem::canonical(directory, error);

    if (error || !std::filesystem::is_directory(root, error) || error)
        return false;

    auto name = Fold(prefix);
    while (name.size() > 1 && name.back() == '\\')
        name.pop_back();

    std::scoped_lock lock(mutex_);

    for (auto& mount : mounts_) {
        if (mount.prefix == name) {
            mount = {std::move(name), std::move(root), readOnly};

            return true;
        }
    }

    mounts_.push_back({std::move(name), std::move(root), readOnly});

    return true;
}

void FileSystem::Clear() {
    std::scoped_lock lock(mutex_);

    mounts_.clear();
    opened_.clear();
}

XNTSTATUS FileSystem::Resolve(std::string_view guestPath, const std::shared_ptr<File>& rootFile,
                              ResolvedPath& resolved) {
    if (guestPath.empty())
        return X_STATUS_OBJECT_NAME_INVALID;

    auto name = Fold(guestPath);
    // the dos devices object directory prefix is a guest namespace marker
    if (name.starts_with("\\??\\"))
        name.erase(0, 4);

    if (name.starts_with("\\dosdevices\\"))
        name.erase(0, 12);

    if (name.empty())
        return X_STATUS_OBJECT_NAME_INVALID;

    for (unsigned char c : name)
        if (c < 32 || c >= 127 || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            return X_STATUS_OBJECT_NAME_INVALID;

    auto& root = resolved.root;
    auto& path = resolved.path;
    auto& readOnly = resolved.readOnly;
    if (rootFile) {
        if (!rootFile->directory_)
            return X_STATUS_NOT_A_DIRECTORY;

        if (name.front() == '\\' || name.find(':') != std::string::npos)
            return X_STATUS_OBJECT_NAME_INVALID;

        root = rootFile->root_;
        path = rootFile->path_;
        readOnly = rootFile->readOnly_;
    } else {
        const MountPoint* selected = nullptr;

        for (const auto& mount : mounts_) {
            if (name.starts_with(mount.prefix)) {
                bool prefix_matches_boundary = false;

                if (name.size() == mount.prefix.size()) {
                    prefix_matches_boundary = true;
                } else if (mount.prefix.back() == ':') {
                    prefix_matches_boundary = true;
                } else if (name[mount.prefix.size()] == '\\') {
                    prefix_matches_boundary = true;
                }

                if (prefix_matches_boundary) {
                    if (!selected) {
                        selected = &mount;
                    } else if (mount.prefix.size() > selected->prefix.size()) {
                        selected = &mount;
                    }
                }
            }
        }
        if (!selected)
            return X_STATUS_OBJECT_PATH_NOT_FOUND;

        root = path = selected->root;
        readOnly = selected->readOnly;
        name.erase(0, selected->prefix.size());

        while (name.starts_with('\\'))
            name.erase(0, 1);
    }
    if (name.find(':') != std::string::npos)
        return X_STATUS_OBJECT_NAME_INVALID;

    std::error_code error;
    size_t begin = 0;
    // resolve each component case insensitively and reject symlinks so guest paths stay inside the mount
    while (begin < name.size()) {
        const auto end = name.find('\\', begin);
        const auto part = name.substr(begin, end == std::string::npos ? name.size() - begin : end - begin);

        begin = end == std::string::npos ? name.size() : end + 1;
        if (part.empty() || part == ".")
            continue;

        if (part == "..") {
            if (path == root)
                return X_STATUS_ACCESS_DENIED;

            path = path.parent_path();
            continue;
        }
        if (part.back() == '.' || part.back() == ' ')
            return X_STATUS_OBJECT_NAME_INVALID;

        // reject dos device names on every host so a file name cannot open a windows device
        const auto stem = part.substr(0, part.find('.'));
        if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul"
            || (stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '1'
                && stem[3] <= '9'))
            return X_STATUS_OBJECT_NAME_INVALID;

        if (!std::filesystem::is_directory(path, error))
            return error ? HostError(error) : X_STATUS_OBJECT_PATH_NOT_FOUND;

        auto candidate = path / part;
        std::filesystem::directory_iterator it(path, error);
        if (error)
            return HostError(error);

        const std::filesystem::directory_iterator endIt;
        for (; it != endIt; it.increment(error)) {
            if (error)
                return HostError(error);

            if (Fold(it->path().filename().string()) == part) {
                candidate = it->path();

                break;
            }
        }

        if (error)
            return HostError(error);

        const auto state = std::filesystem::symlink_status(candidate, error);
        if (error && error != std::errc::no_such_file_or_directory)
            return HostError(error);

        error.clear();
        if (std::filesystem::is_symlink(state))
            return X_STATUS_ACCESS_DENIED;

        path = std::move(candidate);
    }

    return STATUS_SUCCESS;
}

QueryResult FileSystem::Query(std::string_view name, const std::shared_ptr<File>& root) {
    std::scoped_lock lock(mutex_);
    QueryResult result;
    ResolvedPath resolved;
    result.status = Resolve(name, root, resolved);
    if (result.status != STATUS_SUCCESS)
        return result;

    std::error_code error;
    const auto state = std::filesystem::status(resolved.path, error);
    if (error || !std::filesystem::exists(state)) {
        result.status = !error || error == std::errc::no_such_file_or_directory ? X_STATUS_NO_SUCH_FILE :
                                                                                  HostError(error);
        return result;
    }

    const bool directory = std::filesystem::is_directory(state);
    if (!directory && !std::filesystem::is_regular_file(state)) {
        result.status = X_STATUS_NOT_SUPPORTED;
        return result;
    }

    auto& info = result.information;
    if (!directory) {
        info.endOfFile = std::filesystem::file_size(resolved.path, error);
        if (error) {
            result.status = HostError(error);
            return result;
        }
        info.allocationSize = info.endOfFile;
    }

    const auto modified = std::filesystem::last_write_time(resolved.path, error);
    if (error) {
        result.status = HostError(error);
        return result;
    }

    // TODO:
    // change time uses modification time until the backend provides separate metadata
    using NtTicks = std::chrono::duration<int64_t, std::ratio<1, 10000000>>;
    const auto unixTicks
        = std::chrono::duration_cast<NtTicks>(
              std::chrono::clock_cast<std::chrono::system_clock>(modified).time_since_epoch())
              .count();
    constexpr int64_t epochOffset = 116444736000000000;
    info.lastWriteTime = unixTicks < -epochOffset ? 0 : uint64_t(unixTicks) + uint64_t(epochOffset);
    info.changeTime = info.lastWriteTime;

    constexpr auto writePermissions = std::filesystem::perms::owner_write
                                      | std::filesystem::perms::group_write
                                      | std::filesystem::perms::others_write;
    const bool readOnly
        = resolved.readOnly || (state.permissions() & writePermissions) == std::filesystem::perms::none;
    info.attributes = (directory ? 0x10u : 0u) | (readOnly ? 0x1u : 0u);
    if (!info.attributes)
        info.attributes = 0x80;

    return result;
}

OpenResult FileSystem::Open(const OpenRequest& request) {
    std::scoped_lock lock(mutex_);

    OpenResult result;
    const auto fail = [&](XNTSTATUS status) {
        result.status = status;

        return result;
    };

    const uint32_t options = request.options;
    constexpr uint32_t supportedOptions = 0x1 | 0x4 | 0x10 | 0x20 | 0x40 | 0x800 | 0x4000;

    if (uint32_t(request.disposition) > 5 || (request.shareAccess & ~7u) || ((options & 0x41) == 0x41)
        || ((options & 0x30) == 0x30) || ((options & 0x804) == 0x804))
        return fail(X_STATUS_INVALID_PARAMETER);

    if (options & ~supportedOptions)
        return fail(X_STATUS_NOT_SUPPORTED);

    if ((options & 1) && request.disposition != Disposition::Open
        && request.disposition != Disposition::Create && request.disposition != Disposition::OpenIf)
        return fail(X_STATUS_INVALID_PARAMETER);

    ResolvedPath resolved;
    const auto status = Resolve(request.path, request.root, resolved);
    if (status != STATUS_SUCCESS)
        return fail(status);

    const auto& root = resolved.root;
    const auto& path = resolved.path;
    const bool readOnly = resolved.readOnly;
    const uint32_t access = ExpandAccess(request.desiredAccess, readOnly);
    if ((options & 0x30) && !(access & 0x100000))
        return fail(X_STATUS_INVALID_PARAMETER);

    const bool writable = access & (2 | 4 | 0x10 | 0x100 | 0x10000);
    std::error_code error;

    const bool exists = std::filesystem::exists(path, error);
    if (error)
        return fail(HostError(error));

    result.action = exists ? Action::Exists : Action::DoesNotExist;
    if (!exists
        && (request.disposition == Disposition::Open || request.disposition == Disposition::Overwrite))
        return fail(X_STATUS_OBJECT_NAME_NOT_FOUND);

    if (exists && request.disposition == Disposition::Create)
        return fail(X_STATUS_OBJECT_NAME_COLLISION);

    const bool directory = exists ? std::filesystem::is_directory(path, error) : (options & 1) != 0;
    if (error)
        return fail(HostError(error));

    if (directory && (options & 0x40))
        return fail(X_STATUS_FILE_IS_A_DIRECTORY);

    if (!directory && (options & 1))
        return fail(X_STATUS_NOT_A_DIRECTORY);

    if (exists && !directory && !std::filesystem::is_regular_file(path, error))
        return fail(error ? HostError(error) : X_STATUS_NOT_SUPPORTED);

    const bool replace
        = exists
          && (request.disposition == Disposition::Supersede || request.disposition == Disposition::Overwrite
              || request.disposition == Disposition::OverwriteIf);

    if (directory && replace)
        return fail(X_STATUS_INVALID_PARAMETER);

    if (readOnly && (writable || !exists || replace))
        return fail(X_STATUS_MEDIA_WRITE_PROTECTED);

    if (replace && request.disposition == Disposition::Supersede && !(access & 0x10000))
        return fail(X_STATUS_ACCESS_DENIED);

    if (replace && request.disposition != Disposition::Supersede && !(access & 2))
        return fail(X_STATUS_ACCESS_DENIED);

    const uint32_t shareAccess = SharingAccess(access);
    std::erase_if(opened_, [](const auto& file) { return file.expired(); });
    for (const auto& weak : opened_) {
        const auto file = weak.lock();
        if (!file)
            continue;

        bool same = file->path_ == path;
        if (!same && exists) {
            same = std::filesystem::equivalent(file->path_, path, error);
            error.clear();
        }
        if (!same)
            continue;

        if ((shareAccess & ~file->sharing_) || (SharingAccess(file->access_) & ~request.shareAccess))
            return fail(X_STATUS_SHARING_VIOLATION);

        // portable streams cannot replace the directory entry while keeping old handles on the old file
        if (replace && request.disposition == Disposition::Supersede)
            return fail(X_STATUS_NOT_SUPPORTED);
    }

    auto file = std::make_shared<File>();
    file->root_ = root;
    file->path_ = path;
    file->readOnly_ = readOnly;
    file->directory_ = directory;
    file->synchronous_ = options & 0x30;
    file->access_ = access;
    file->sharing_ = request.shareAccess;

    // allocate the sharing slot before any host mutation so allocation failure cannot lose an open
    // reservation
    opened_.push_back(file);

    if (directory) {
        if (!exists && !std::filesystem::create_directory(path, error))
            return fail(error ? HostError(error) : X_STATUS_OBJECT_NAME_COLLISION);
    } else {
        const bool supersede = replace && request.disposition == Disposition::Supersede;
        // supersede replaces the directory entry rather than truncating other hard links to the old file
        if (supersede && !std::filesystem::remove(path, error))
            return fail(error ? HostError(error) : X_STATUS_OBJECT_PATH_NOT_FOUND);

        auto mode = std::ios::binary | std::ios::in;
        if (!exists || replace || (access & 6))
            mode |= std::ios::out;

        if (!exists || supersede)
            mode |= std::ios::trunc | std::ios::noreplace;
        else if (replace)
            mode |= std::ios::trunc;

        errno = 0;
        file->stream_.open(path, mode);
        if (!file->stream_.is_open())
            return fail(errno ? HostError(std::error_code(errno, std::generic_category())) :
                                X_STATUS_ACCESS_DENIED);
    }

    // allocation size is a reservation hint and does not change the end of file
    result.action = !exists                                       ? Action::Created :
                    !replace                                      ? Action::Opened :
                    request.disposition == Disposition::Supersede ? Action::Superseded :
                                                                    Action::Overwritten;

    result.file = std::move(file);

    return result;
}

}  // namespace vfs
