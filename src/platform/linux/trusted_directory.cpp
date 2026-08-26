// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/trusted_directory.hpp>

#include <fcntl.h>
#include <linux/openat2.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>
#include <utility>

#include <core/errors.hpp>
#include <config/model/validation.hpp>
#include <platform/linux/safe_directory_root.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

namespace {

[[noreturn]] void throw_directory_error(const std::string& operation, const fs::path& path, int error) {
    throw ValidationError(operation + " " + path.string() + ": " + std::strerror(error));
}

int open_directory_at(int parent_fd, const fs::path& path) {
    struct open_how how {};
    how.flags = O_RDONLY | O_DIRECTORY | O_CLOEXEC;
    how.resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS;
    return static_cast<int>(syscall(SYS_openat2, parent_fd, path.c_str(), &how, sizeof(how)));
}

void validate_directory_descriptor(int fd, const fs::path& path, uid_t trusted_owner) {
    struct stat status {};
    if (fstat(fd, &status) != 0) {
        throw_directory_error("cannot inspect trusted directory", path, errno);
    }
    if (!S_ISDIR(status.st_mode)) {
        throw ValidationError("trusted path component is not a directory: " + path.string());
    }
    if (status.st_uid != trusted_owner) {
        throw ValidationError("trusted path component has an untrusted owner: " + path.string());
    }
    if ((status.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        throw ValidationError("trusted path component is writable by group or others: " + path.string());
    }
}

SafeDirectoryHandle open_trusted_root(const fs::path& trusted_root, uid_t trusted_owner) {
    int slash_fd = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (slash_fd < 0) {
        throw_directory_error("cannot open filesystem root for", trusted_root, errno);
    }
    SafeDirectoryHandle slash(slash_fd);
    fs::path relative = trusted_root.lexically_relative("/");
    if (relative.empty()) {
        relative = ".";
    }
    int root_fd = open_directory_at(slash.fd(), relative);
    if (root_fd < 0) {
        throw_directory_error("cannot open trusted directory root", trusted_root, errno);
    }
    SafeDirectoryHandle root(root_fd);
    validate_directory_descriptor(root.fd(), trusted_root, trusted_owner);
    return root;
}

SafeDirectoryHandle traverse_trusted_directory(
    const fs::path& path,
    const fs::path& trusted_root,
    uid_t trusted_owner,
    bool create,
    mode_t mode
) {
    fs::path normalized_root = normalized_path(trusted_root);
    fs::path normalized = normalized_path(path);
    if (!normalized_root.is_absolute() || !normalized.is_absolute() || !path_is_within(normalized, normalized_root)) {
        throw ValidationError("trusted directory path escapes " + normalized_root.string() + ": " + path.string());
    }

    SafeDirectoryHandle current = open_trusted_root(normalized_root, trusted_owner);
    fs::path display = normalized_root;
    fs::path relative = normalized.lexically_relative(normalized_root);
    for (const fs::path& component : relative) {
        if (component == ".") {
            continue;
        }
        display /= component;
        int next_fd = open_directory_at(current.fd(), component);
        if (next_fd < 0 && errno == ENOENT && create) {
            if (mkdirat(current.fd(), component.c_str(), mode) != 0 && errno != EEXIST) {
                throw_directory_error("cannot create trusted directory", display, errno);
            }
            next_fd = open_directory_at(current.fd(), component);
        }
        if (next_fd < 0) {
            throw_directory_error("cannot open trusted directory without symlinks", display, errno);
        }
        SafeDirectoryHandle next(next_fd);
        validate_directory_descriptor(next.fd(), display, trusted_owner);
        current = std::move(next);
    }
    return current;
}

} // namespace

void validate_trusted_directory(const fs::path& path, const fs::path& trusted_root, uid_t trusted_owner) {
    (void)traverse_trusted_directory(path, trusted_root, trusted_owner, false, 0);
}

void ensure_trusted_directory(
    const fs::path& path,
    unsigned int mode,
    const fs::path& trusted_root,
    uid_t trusted_owner
) {
    SafeDirectoryHandle directory = traverse_trusted_directory(
        path,
        trusted_root,
        trusted_owner,
        true,
        static_cast<mode_t>(mode)
    );
    if (fchmod(directory.fd(), static_cast<mode_t>(mode)) != 0) {
        throw_directory_error("cannot set trusted directory permissions on", path, errno);
    }
}

} // namespace btrfsbackup
