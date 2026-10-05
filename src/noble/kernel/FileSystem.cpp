#include "FileSystem.h"

#include <algorithm>
#include <cerrno>
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

Status HostError(const std::error_code& error) {
    if (error == std::errc::permission_denied)
        return Status::AccessDenied;

    if (error == std::errc::no_such_file_or_directory)
        return Status::ObjectPathNotFound;

    if (error == std::errc::file_exists)
        return Status::ObjectNameCollision;

    if (error == std::errc::not_a_directory)
        return Status::NotADirectory;

    if (error == std::errc::read_only_file_system)
        return Status::MediaWriteProtected;

    if (error == std::errc::too_many_files_open || error == std::errc::not_enough_memory)
        return Status::InsufficientResources;

    return Status::IOError;
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
        return {Status::InvalidDeviceRequest, 0};

    if (!(access_ & 1))
        return {Status::AccessDenied, 0};

    if (buffer.empty())
        return {Status::Success, 0};

    const auto position = offset.value_or(position_);
    if (position > uint64_t(std::numeric_limits<std::streamoff>::max())
        || buffer.size() > uint64_t(std::numeric_limits<std::streamsize>::max())
        || buffer.size() > UINT32_MAX)
        return {Status::InvalidParameter, 0};

    stream_.clear();
    stream_.seekg(static_cast<std::streamoff>(position), std::ios::beg);
    if (!stream_)
        return {Status::IOError, 0};

    stream_.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
    const auto count = static_cast<uint32_t>(stream_.gcount());
    if (stream_.bad() || (stream_.fail() && !stream_.eof()))
        return {Status::IOError, count};

    if (!count && stream_.eof())
        return {Status::EndOfFile, 0};

    position_ = position + count;

    return {Status::Success, count};
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

OpenResult FileSystem::Open(const OpenRequest& request) {
    std::scoped_lock lock(mutex_);

    OpenResult result;
    const auto fail = [&](Status status) {
        result.status = status;

        return result;
    };

    const uint32_t options = request.options;
    constexpr uint32_t supportedOptions = 0x1 | 0x4 | 0x10 | 0x20 | 0x40 | 0x800 | 0x4000;

    if (uint32_t(request.disposition) > 5 || (request.shareAccess & ~7u) || ((options & 0x41) == 0x41)
        || ((options & 0x30) == 0x30) || ((options & 0x804) == 0x804))
        return fail(Status::InvalidParameter);

    if (options & ~supportedOptions)
        return fail(Status::NotSupported);

    if ((options & 1) && request.disposition != Disposition::Open
        && request.disposition != Disposition::Create && request.disposition != Disposition::OpenIf)
        return fail(Status::InvalidParameter);

    if (request.path.empty())
        return fail(Status::ObjectNameInvalid);

    for (unsigned char c : request.path)
        if (c < 32 || c >= 127 || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            return fail(Status::ObjectNameInvalid);

    auto name = Fold(request.path);
    // the dos devices object directory prefix is a guest namespace marker
    if (name.starts_with("\\??\\"))
        name.erase(0, 4);

    if (name.starts_with("\\dosdevices\\"))
        name.erase(0, 12);

    if (name.empty())
        return fail(Status::ObjectNameInvalid);

    std::filesystem::path root, path;
    bool readOnly = true;
    if (request.root) {
        if (!request.root->directory_)
            return fail(Status::NotADirectory);

        if (name.front() == '\\' || name.find(':') != std::string::npos)
            return fail(Status::ObjectNameInvalid);

        root = request.root->root_;
        path = request.root->path_;
        readOnly = request.root->readOnly_;
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
            return fail(Status::ObjectPathNotFound);

        root = path = selected->root;
        readOnly = selected->readOnly;
        name.erase(0, selected->prefix.size());

        while (name.starts_with('\\'))
            name.erase(0, 1);
    }
    if (name.find(':') != std::string::npos)
        return fail(Status::ObjectNameInvalid);

    const uint32_t access = ExpandAccess(request.desiredAccess, readOnly);
    if ((options & 0x30) && !(access & 0x100000))
        return fail(Status::InvalidParameter);

    const bool writable = access & (2 | 4 | 0x10 | 0x100 | 0x10000);
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
                return fail(Status::AccessDenied);

            path = path.parent_path();
            continue;
        }
        if (part.back() == '.' || part.back() == ' ')
            return fail(Status::ObjectNameInvalid);

        // reject dos device names on every host so a file name cannot open a windows device
        const auto stem = part.substr(0, part.find('.'));
        if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul"
            || (stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '1'
                && stem[3] <= '9'))
            return fail(Status::ObjectNameInvalid);

        if (!std::filesystem::is_directory(path, error))
            return fail(error ? HostError(error) : Status::ObjectPathNotFound);

        auto candidate = path / part;
        std::filesystem::directory_iterator it(path, error);
        if (error)
            return fail(HostError(error));

        const std::filesystem::directory_iterator endIt;
        for (; it != endIt; it.increment(error)) {
            if (error)
                return fail(HostError(error));

            if (Fold(it->path().filename().string()) == part) {
                candidate = it->path();

                break;
            }
        }

        if (error)
            return fail(HostError(error));

        const auto state = std::filesystem::symlink_status(candidate, error);
        if (error && error != std::errc::no_such_file_or_directory)
            return fail(HostError(error));

        error.clear();
        if (std::filesystem::is_symlink(state))
            return fail(Status::AccessDenied);

        path = std::move(candidate);
    }

    const bool exists = std::filesystem::exists(path, error);
    if (error)
        return fail(HostError(error));

    result.action = exists ? Action::Exists : Action::DoesNotExist;
    if (!exists
        && (request.disposition == Disposition::Open || request.disposition == Disposition::Overwrite))
        return fail(Status::ObjectNameNotFound);

    if (exists && request.disposition == Disposition::Create)
        return fail(Status::ObjectNameCollision);

    const bool directory = exists ? std::filesystem::is_directory(path, error) : (options & 1) != 0;
    if (error)
        return fail(HostError(error));

    if (directory && (options & 0x40))
        return fail(Status::FileIsADirectory);

    if (!directory && (options & 1))
        return fail(Status::NotADirectory);

    if (exists && !directory && !std::filesystem::is_regular_file(path, error))
        return fail(error ? HostError(error) : Status::NotSupported);

    const bool replace
        = exists
          && (request.disposition == Disposition::Supersede || request.disposition == Disposition::Overwrite
              || request.disposition == Disposition::OverwriteIf);

    if (directory && replace)
        return fail(Status::InvalidParameter);

    if (readOnly && (writable || !exists || replace))
        return fail(Status::MediaWriteProtected);

    if (replace && request.disposition == Disposition::Supersede && !(access & 0x10000))
        return fail(Status::AccessDenied);

    if (replace && request.disposition != Disposition::Supersede && !(access & 2))
        return fail(Status::AccessDenied);

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
            return fail(Status::SharingViolation);

        // portable streams cannot replace the directory entry while keeping old handles on the old file
        if (replace && request.disposition == Disposition::Supersede)
            return fail(Status::NotSupported);
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
            return fail(error ? HostError(error) : Status::ObjectNameCollision);
    } else {
        const bool supersede = replace && request.disposition == Disposition::Supersede;
        // supersede replaces the directory entry rather than truncating other hard links to the old file
        if (supersede && !std::filesystem::remove(path, error))
            return fail(error ? HostError(error) : Status::ObjectNameNotFound);

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
                                Status::AccessDenied);
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
