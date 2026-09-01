// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "SecureBrowsePath.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <memory>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace btrfsbackup::kde::kio {
namespace {

[[noreturn]] void path_error(const char* operation) {
    throw std::system_error(errno, std::generic_category(), operation);
}

std::vector<std::string> validated_components(const fs::path& relative) {
    if (relative.is_absolute())
        throw std::invalid_argument("browse path must be relative");
    std::vector<std::string> components;
    for (const fs::path& component : relative) {
        const std::string value = component.string();
        if (value.empty() || value == ".")
            continue;
        if (value == ".." || value.find('/') != std::string::npos || value.find('\0') != std::string::npos)
            throw std::invalid_argument("browse path contains an unsafe component");
        components.push_back(value);
    }
    return components;
}

SecureBrowseFile open_beneath(int root_descriptor, const fs::path& relative, int final_flags) {
    SecureBrowseFile current(openat(
        root_descriptor,
        ".",
        O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW
    ));
    if (!current.valid())
        path_error("cannot open browse session root");
    const std::vector<std::string> components = validated_components(relative);
    if (components.empty()) {
        SecureBrowseFile result(openat(current.descriptor(), ".", final_flags | O_CLOEXEC | O_NOFOLLOW));
        if (!result.valid())
            path_error("cannot open browse session entry");
        return result;
    }
    for (std::size_t index = 0; index + 1 < components.size(); ++index) {
        SecureBrowseFile next(openat(
            current.descriptor(),
            components[index].c_str(),
            O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW
        ));
        if (!next.valid())
            path_error("cannot traverse browse session entry");
        current = std::move(next);
    }
    SecureBrowseFile result(openat(
        current.descriptor(),
        components.back().c_str(),
        final_flags | O_CLOEXEC | O_NOFOLLOW
    ));
    if (!result.valid())
        path_error("cannot open browse session entry");
    return result;
}

SecureBrowseFile open_root(const fs::path& root) {
    SecureBrowseFile result(open(root.c_str(), O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (!result.valid())
        path_error("cannot open browse session root");
    return result;
}

struct DirectoryCloser {
    void operator()(DIR* directory) const noexcept {
        if (directory != nullptr)
            closedir(directory);
    }
};

} // namespace

SecureBrowseFile::SecureBrowseFile(int descriptor) noexcept : descriptor_(descriptor) {
}

SecureBrowseFile::~SecureBrowseFile() noexcept {
    if (descriptor_ >= 0)
        close(descriptor_);
}

SecureBrowseFile::SecureBrowseFile(SecureBrowseFile&& other) noexcept : descriptor_(other.release()) {
}

SecureBrowseFile& SecureBrowseFile::operator=(SecureBrowseFile&& other) noexcept {
    if (this != &other) {
        if (descriptor_ >= 0)
            close(descriptor_);
        descriptor_ = other.release();
    }
    return *this;
}

int SecureBrowseFile::descriptor() const noexcept {
    return descriptor_;
}

bool SecureBrowseFile::valid() const noexcept {
    return descriptor_ >= 0;
}

int SecureBrowseFile::release() noexcept {
    const int descriptor = descriptor_;
    descriptor_ = -1;
    return descriptor;
}

BrowseDirectoryLimitError::BrowseDirectoryLimitError()
    : std::runtime_error("browse directory exceeds the entry limit") {
}

SecureBrowseFile open_browse_regular_file(const fs::path& root, const fs::path& relative) {
    const SecureBrowseFile root_descriptor = open_root(root);
    return open_browse_regular_file(root_descriptor.descriptor(), relative);
}

SecureBrowseFile open_browse_regular_file(int root_descriptor, const fs::path& relative) {
    SecureBrowseFile result = open_beneath(root_descriptor, relative, O_RDONLY | O_NONBLOCK);
    struct stat status{};
    if (fstat(result.descriptor(), &status) != 0)
        path_error("cannot inspect browse file");
    if (!S_ISREG(status.st_mode))
        throw std::invalid_argument("browse entry is not a regular file");
    return result;
}

SecureBrowseFile open_browse_directory(const fs::path& root, const fs::path& relative) {
    const SecureBrowseFile root_descriptor = open_root(root);
    return open_browse_directory(root_descriptor.descriptor(), relative);
}

SecureBrowseFile open_browse_directory(int root_descriptor, const fs::path& relative) {
    return open_beneath(root_descriptor, relative, O_RDONLY | O_DIRECTORY);
}

SecureBrowseFile open_browse_metadata(const fs::path& root, const fs::path& relative) {
    const SecureBrowseFile root_descriptor = open_root(root);
    return open_browse_metadata(root_descriptor.descriptor(), relative);
}

SecureBrowseFile open_browse_metadata(int root_descriptor, const fs::path& relative) {
    SecureBrowseFile result = open_beneath(root_descriptor, relative, O_PATH);
    struct stat status{};
    if (fstat(result.descriptor(), &status) != 0)
        path_error("cannot inspect browse entry");
    if (!S_ISREG(status.st_mode) && !S_ISDIR(status.st_mode))
        throw std::invalid_argument("browse entry has an unsupported type");
    return result;
}

std::vector<BrowseDirectoryEntry> list_browse_directory(int descriptor, std::size_t maximum_entries) {
    const int duplicate = openat(descriptor, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (duplicate < 0)
        path_error("cannot duplicate browse directory descriptor");
    std::unique_ptr<DIR, DirectoryCloser> directory(fdopendir(duplicate));
    if (!directory) {
        close(duplicate);
        path_error("cannot open browse directory stream");
    }
    std::vector<BrowseDirectoryEntry> result;
    errno = 0;
    while (dirent* entry = readdir(directory.get())) {
        const std::string name = entry->d_name;
        if (name == "." || name == ".." || name == ".incoming")
            continue;
        struct stat status{};
        if (fstatat(descriptor, name.c_str(), &status, AT_SYMLINK_NOFOLLOW) != 0)
            path_error("cannot inspect browse directory entry");
        if (S_ISLNK(status.st_mode) || (!S_ISREG(status.st_mode) && !S_ISDIR(status.st_mode)))
            continue;
        if (result.size() >= maximum_entries)
            throw BrowseDirectoryLimitError{};
        result.push_back({
            name,
            S_ISDIR(status.st_mode) ? BrowseEntryKind::Directory : BrowseEntryKind::RegularFile,
            S_ISREG(status.st_mode) ? static_cast<std::uintmax_t>(status.st_size) : 0,
            status.st_mode,
            status.st_mtim.tv_sec,
        });
        errno = 0;
    }
    if (errno != 0)
        path_error("cannot read browse directory");
    return result;
}

} // namespace btrfsbackup::kde::kio
