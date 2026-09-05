// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/BrowseFilesystemAccess.hpp>

#include <daemon/control/BrowseDirectoryPageCollector.hpp>
#include <daemon/dbus/ManagerErrors.hpp>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <memory>
#include <stdexcept>
#include <system_error>

namespace fs = std::filesystem;

namespace btrfsbackup::daemon::control {
namespace {

using btrfsbackup::platform::linux::OwnedFileDescriptor;

[[noreturn]] void path_error(const char* operation) {
    throw std::system_error(errno, std::generic_category(), operation);
}

std::vector<std::string> validated_components(const fs::path& relative) {
    if (relative.is_absolute())
        throw std::invalid_argument("browse path must be relative");
    std::vector<std::string> result;
    for (const fs::path& component : relative) {
        const std::string value = component.string();
        if (value.empty() || value == ".")
            continue;
        if (value == ".." || value.find('/') != std::string::npos || value.find('\0') != std::string::npos)
            throw std::invalid_argument("browse path contains an unsafe component");
        result.push_back(value);
    }
    return result;
}

OwnedFileDescriptor open_beneath(int root, const fs::path& relative, int final_flags) {
    OwnedFileDescriptor current(openat(root, ".", O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (!current.valid())
        path_error("cannot open browse root");
    const auto components = validated_components(relative);
    if (components.empty()) {
        OwnedFileDescriptor result(openat(current.get(), ".", final_flags | O_CLOEXEC | O_NOFOLLOW));
        if (!result.valid())
            path_error("cannot open browse entry");
        return result;
    }
    for (std::size_t index = 0; index + 1 < components.size(); ++index) {
        OwnedFileDescriptor next(openat(
            current.get(),
            components[index].c_str(),
            O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW
        ));
        if (!next.valid())
            path_error("cannot traverse browse entry");
        current = std::move(next);
    }
    OwnedFileDescriptor result(openat(
        current.get(),
        components.back().c_str(),
        final_flags | O_CLOEXEC | O_NOFOLLOW
    ));
    if (!result.valid())
        path_error("cannot open browse entry");
    return result;
}

OwnedFileDescriptor session_root(const fs::path& root) {
    OwnedFileDescriptor descriptor(::open(root.c_str(), O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (!descriptor.valid())
        path_error("cannot open browse session view");
    return descriptor;
}

struct DirectoryCloser {
    void operator()(DIR* directory) const noexcept {
        if (directory != nullptr)
            closedir(directory);
    }
};

std::unique_ptr<DIR, DirectoryCloser> directory_stream(int descriptor) {
    const int duplicate = dup(descriptor);
    if (duplicate < 0)
        path_error("cannot duplicate browse directory");
    std::unique_ptr<DIR, DirectoryCloser> stream(fdopendir(duplicate));
    if (!stream) {
        ::close(duplicate);
        path_error("cannot open browse directory stream");
    }
    return stream;
}

BrowseEntryInfo entry_info(int parent, const std::string& name) {
    struct stat status{};
    if (fstatat(parent, name.c_str(), &status, AT_SYMLINK_NOFOLLOW) != 0)
        path_error("cannot inspect browse entry");
    if (S_ISLNK(status.st_mode) || (!S_ISREG(status.st_mode) && !S_ISDIR(status.st_mode)))
        throw std::invalid_argument("browse entry has an unsupported type");
    return {
        name,
        S_ISDIR(status.st_mode),
        S_ISREG(status.st_mode) ? static_cast<std::uint64_t>(status.st_size) : 0,
        static_cast<std::uint32_t>(status.st_mode),
        status.st_mtim.tv_sec,
    };
}

} // namespace

fs::path BrowseFilesystemAccess::normalize_relative_path(const fs::path& relative_path) {
    fs::path result;
    for (const std::string& component : validated_components(relative_path))
        result /= component;
    return result.empty() ? fs::path{"."} : result;
}

std::vector<BrowseEntryInfo> BrowseFilesystemAccess::list_directory(
    const fs::path& root,
    const fs::path& relative_path,
    std::size_t maximum_entries
) const {
    OwnedFileDescriptor root_descriptor = session_root(root);
    OwnedFileDescriptor directory = open_beneath(root_descriptor.get(), relative_path, O_RDONLY | O_DIRECTORY);
    auto stream = directory_stream(directory.get());
    std::vector<BrowseEntryInfo> result;
    errno = 0;
    while (dirent* item = readdir(stream.get())) {
        const std::string name = item->d_name;
        if (name == "." || name == ".." || name == ".incoming")
            continue;
        try {
            BrowseEntryInfo info = entry_info(directory.get(), name);
            if (result.size() >= maximum_entries)
                throw dbus::ManagerOperationError(dbus::ManagerErrorCode::InvalidRequest, "browse directory exceeds the safe entry limit");
            result.push_back(std::move(info));
        } catch (const std::invalid_argument&) {
            continue;
        }
        errno = 0;
    }
    if (errno != 0)
        path_error("cannot read browse directory");
    return result;
}

BrowseDirectoryPage BrowseFilesystemAccess::list_directory_page(
    const fs::path& root,
    const fs::path& relative_path,
    const std::string& after_name,
    std::size_t maximum_entries
) const {
    OwnedFileDescriptor root_descriptor = session_root(root);
    OwnedFileDescriptor directory = open_beneath(root_descriptor.get(), relative_path, O_RDONLY | O_DIRECTORY);
    auto stream = directory_stream(directory.get());
    BrowseDirectoryPageCollector entries(maximum_entries);
    errno = 0;
    while (dirent* item = readdir(stream.get())) {
        const std::string name = item->d_name;
        if (name == "." || name == ".." || name == ".incoming" || name <= after_name)
            continue;
        try {
            entries.add(entry_info(directory.get(), name));
        } catch (const std::invalid_argument&) {
            continue;
        }
        errno = 0;
    }
    if (errno != 0)
        path_error("cannot read browse directory");
    return entries.finish();
}

BrowseEntryInfo BrowseFilesystemAccess::inspect_entry(
    const fs::path& root,
    const fs::path& relative_path
) const {
    OwnedFileDescriptor root_descriptor = session_root(root);
    OwnedFileDescriptor entry = open_beneath(root_descriptor.get(), relative_path, O_PATH);
    struct stat status{};
    if (fstat(entry.get(), &status) != 0)
        path_error("cannot inspect browse entry");
    if (!S_ISREG(status.st_mode) && !S_ISDIR(status.st_mode))
        throw std::invalid_argument("browse entry has an unsupported type");
    return {
        relative_path.filename().string(),
        S_ISDIR(status.st_mode),
        S_ISREG(status.st_mode) ? static_cast<std::uint64_t>(status.st_size) : 0,
        static_cast<std::uint32_t>(status.st_mode),
        status.st_mtim.tv_sec,
    };
}

OwnedFileDescriptor BrowseFilesystemAccess::open_file(
    const fs::path& root,
    const fs::path& relative_path
) const {
    OwnedFileDescriptor root_descriptor = session_root(root);
    OwnedFileDescriptor result = open_beneath(root_descriptor.get(), relative_path, O_RDONLY | O_NONBLOCK);
    struct stat status{};
    if (fstat(result.get(), &status) != 0)
        path_error("cannot inspect browse file");
    if (!S_ISREG(status.st_mode))
        throw std::invalid_argument("browse entry is not a regular file");
    return result;
}

OwnedFileDescriptor BrowseFilesystemAccess::open_root(const fs::path& root) const {
    OwnedFileDescriptor result(::open(root.c_str(), O_PATH | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (!result.valid())
        path_error("cannot open browse session root");
    return result;
}

} // namespace btrfsbackup::daemon::control
