// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/filesystem/PosixDurableFileOperations.hpp>

#include <string>
#include <system_error>
#include <sys/types.h>

#include <core/Errors.hpp>
#include <platform/linux/filesystem/FileIo.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::platform::linux::filesystem {

void PosixDurableFileOperations::ensure_directory(const fs::path& path, fs::perms permissions) {
    fs::create_directories(path);
    std::error_code error;
    fs::permissions(path, permissions, fs::perm_options::replace, error);
    if (error) {
        throw ValidationError("cannot set permissions on " + path.string() + ": " + error.message());
    }
}

void PosixDurableFileOperations::write_atomically(
    const fs::path& path,
    std::string_view data,
    fs::perms permissions
) {
    atomic_write(path, std::string(data), static_cast<mode_t>(permissions));
}

void PosixDurableFileOperations::remove_durably(const fs::path& path) {
    std::error_code error;
    const bool removed = fs::remove(path, error);
    if (error) {
        throw ValidationError("cannot remove " + path.string() + ": " + error.message());
    }
    if (removed) {
        fsync_dir(path.parent_path());
    }
}

} // namespace btrfsbackup::platform::linux::filesystem
