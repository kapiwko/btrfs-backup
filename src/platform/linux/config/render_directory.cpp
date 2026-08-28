// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/config/render_directory.hpp>

#include <fcntl.h>
#include <linux/fs.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <ios>
#include <iterator>
#include <string>
#include <system_error>
#include <vector>

#include <core/errors.hpp>
#include <platform/linux/file_io.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::platform::linux {

namespace {

constexpr const char* marker_content = "btrfs-backup-render-root-v1\n";

struct DirectoryIdentity {
    dev_t device;
    ino_t inode;
};

[[noreturn]] void throw_path_error(const std::string& message, const fs::path& path, int error) {
    throw ValidationError(message + " " + path.string() + ": " + std::strerror(error));
}

DirectoryIdentity directory_identity(const fs::path& path) {
    struct stat status{};
    if (lstat(path.c_str(), &status) != 0) {
        throw_path_error("cannot inspect render output directory", path, errno);
    }
    if (!S_ISDIR(status.st_mode)) {
        throw ValidationError("render output is not a directory: " + path.string());
    }
    if (status.st_uid != geteuid()) {
        throw ValidationError("render output directory is not owned by the current user: " + path.string());
    }
    return {.device = status.st_dev, .inode = status.st_ino};
}

bool same_directory(const fs::path& path, const DirectoryIdentity& expected) {
    struct stat status{};
    return lstat(path.c_str(), &status) == 0 && S_ISDIR(status.st_mode) && status.st_uid == geteuid() && status.st_dev == expected.device && status.st_ino == expected.inode;
}

uid_t filesystem_root_owner() {
    static const uid_t owner = [] {
        struct stat status{};
        if (lstat("/", &status) != 0) {
            throw_path_error("cannot inspect filesystem root", "/", errno);
        }
        return status.st_uid;
    }();
    return owner;
}

bool trusted_owner(uid_t owner) {
    return owner == 0 || owner == geteuid() || owner == filesystem_root_owner();
}

bool directory_empty(const fs::path& path) {
    std::error_code error;
    fs::directory_iterator it(path, error);
    if (error) {
        throw ValidationError("cannot inspect render output directory " + path.string() + ": " + error.message());
    }
    return it == fs::directory_iterator();
}

bool valid_marker(const fs::path& directory) {
    const fs::path marker = directory / render_root_marker;
    struct stat status{};
    if (lstat(marker.c_str(), &status) != 0) {
        if (errno == ENOENT) {
            return false;
        }
        throw_path_error("cannot inspect render root marker", marker, errno);
    }
    if (!S_ISREG(status.st_mode) || status.st_uid != geteuid() || (status.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
        return false;
    }
    std::ifstream input(marker, std::ios::binary);
    if (!input) {
        return false;
    }
    const std::string content{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    return content == marker_content;
}

DirectoryIdentity require_replaceable_directory(const fs::path& path) {
    const DirectoryIdentity identity = directory_identity(path);
    if (!directory_empty(path) && !valid_marker(path)) {
        throw ValidationError(
            "refusing to replace non-empty output directory without " + std::string(render_root_marker) + ": " + path.string()
        );
    }
    return identity;
}

void validate_parent_chain(const fs::path& parent) {
    std::vector<fs::path> directories;
    for (fs::path current = parent; !current.empty(); current = current.parent_path()) {
        directories.push_back(current);
        if (current == current.root_path()) {
            break;
        }
    }
    for (auto it = directories.rbegin(); it != directories.rend(); ++it) {
        struct stat status{};
        if (lstat(it->c_str(), &status) != 0) {
            throw_path_error("cannot inspect render output parent", *it, errno);
        }
        if (!S_ISDIR(status.st_mode)) {
            throw ValidationError("render output parent is not a directory: " + it->string());
        }
        const bool writable_by_current_user = faccessat(AT_FDCWD, it->c_str(), W_OK, AT_EACCESS) == 0;
        if (writable_by_current_user && !trusted_owner(status.st_uid)) {
            throw ValidationError("render output parent is owned by an untrusted user: " + it->string());
        }
        if ((status.st_mode & (S_IWGRP | S_IWOTH)) != 0) {
            if ((status.st_mode & S_ISVTX) == 0 || !trusted_owner(status.st_uid)) {
                throw ValidationError("render output parent is writable by an untrusted user: " + it->string());
            }
        }
    }
}

void prepare_parent(const fs::path& parent) {
    fs::path existing = parent;
    std::error_code error;
    while (!existing.empty() && !fs::exists(existing, error)) {
        if (error) {
            throw ValidationError("cannot inspect render output parent " + existing.string() + ": " + error.message());
        }
        existing = existing.parent_path();
    }
    validate_parent_chain(existing);
    fs::create_directories(parent);
    validate_parent_chain(parent);
}

fs::path create_staging_directory(const fs::path& output) {
    const fs::path parent = output.parent_path();
    prepare_parent(parent);
    std::string pattern = (parent / ("." + output.filename().string() + ".stage-XXXXXX")).string();
    std::vector<char> writable(pattern.begin(), pattern.end());
    writable.push_back('\0');
    char* created = mkdtemp(writable.data());
    if (created == nullptr) {
        throw_path_error("cannot create render staging directory for", output, errno);
    }
    return created;
}

void exchange_directories(const fs::path& first, const fs::path& second) {
    long result;
    do {
        result = syscall(SYS_renameat2, AT_FDCWD, first.c_str(), AT_FDCWD, second.c_str(), RENAME_EXCHANGE);
    } while (result < 0 && errno == EINTR);
    if (result < 0) {
        throw_path_error("cannot atomically replace render output", second, errno);
    }
}

void remove_staging(const fs::path& path) noexcept {
    std::error_code error;
    fs::remove_all(path, error);
}

} // namespace

void replace_render_directory(
    const fs::path& output_dir,
    const std::function<void(const fs::path&)>& render,
    const std::function<void(const fs::path&)>& validate
) {
    const fs::path output = fs::absolute(output_dir).lexically_normal();
    std::error_code status_error;
    const fs::file_status status = fs::symlink_status(output, status_error);
    const bool exists = !status_error && fs::exists(status);
    if (status_error && status_error != std::errc::no_such_file_or_directory) {
        throw ValidationError("cannot inspect render output " + output.string() + ": " + status_error.message());
    }

    DirectoryIdentity previous{};
    if (exists) {
        previous = require_replaceable_directory(output);
    }

    const fs::path staging = create_staging_directory(output);
    bool staging_contains_previous = false;
    try {
        render(staging);
        validate(staging);
        atomic_write(staging / render_root_marker, marker_content, 0600);

        if (!exists) {
            std::error_code rename_error;
            fs::rename(staging, output, rename_error);
            if (rename_error) {
                throw ValidationError("cannot publish render output " + output.string() + ": " + rename_error.message());
            }
            try {
                fsync_dir(output.parent_path());
            } catch (...) {
                std::error_code rollback_error;
                fs::rename(output, staging, rollback_error);
                throw;
            }
            return;
        }

        if (!same_directory(output, previous)) {
            throw ValidationError("render output changed while replacement was being prepared: " + output.string());
        }
        exchange_directories(staging, output);
        staging_contains_previous = true;
        try {
            if (!same_directory(staging, previous) || (!directory_empty(staging) && !valid_marker(staging))) {
                throw ValidationError("render output changed during replacement: " + output.string());
            }
            fsync_dir(output.parent_path());
        } catch (...) {
            exchange_directories(staging, output);
            staging_contains_previous = false;
            throw;
        }
    } catch (...) {
        if (!staging_contains_previous) {
            remove_staging(staging);
        }
        throw;
    }
    remove_staging(staging);
    try {
        fsync_dir(output.parent_path());
    } catch (...) {
    }
}

} // namespace btrfsbackup::platform::linux
