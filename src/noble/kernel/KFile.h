#pragma once

#include "FileSystem.h"
#include "KObject.h"

// own an open filesystem object through the process handle table
class KFile final : public KernelObject {
public:
    explicit KFile(std::shared_ptr<vfs::File> file)
        : KernelObject(KernelObjectType::KFile), file_(std::move(file)) {}
    const std::shared_ptr<vfs::File>& file() const noexcept { return file_; }

private:
    std::shared_ptr<vfs::File> file_;
};
