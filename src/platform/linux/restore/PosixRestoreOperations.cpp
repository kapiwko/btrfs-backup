// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/restore/PosixRestoreOperations.hpp>

#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include <btrfsutil.h>
#include <sys/stat.h>
#include <unistd.h>

#include <restore/RestoreError.hpp>

namespace btrfsbackup::platform::linux::restore {

namespace {

struct SourceIdentity {
    dev_t device = 0;
};

struct stat lstat_or_throw(const std::filesystem::path& path) {
    struct stat status{};
    if (::lstat(path.c_str(), &status) != 0) {
        throw btrfsbackup::restore::RestoreError(
            btrfsbackup::restore::RestoreErrorCode::CopyFailed,
            "could not inspect restore path " + path.string() + ": " + std::strerror(errno)
        );
    }
    return status;
}

void throw_if_cancelled(CancellationToken& cancellation) {
    if (cancellation.cancellation_requested()) {
        throw btrfsbackup::restore::RestoreError(
            btrfsbackup::restore::RestoreErrorCode::Cancelled,
            "restore was cancelled"
        );
    }
}

void verify_regular_file(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    CancellationToken& cancellation
) {
    std::ifstream left(source, std::ios::binary);
    std::ifstream right(destination, std::ios::binary);
    std::array<char, 64 * 1024> left_buffer{};
    std::array<char, 64 * 1024> right_buffer{};
    while (left || right) {
        throw_if_cancelled(cancellation);
        left.read(left_buffer.data(), static_cast<std::streamsize>(left_buffer.size()));
        right.read(right_buffer.data(), static_cast<std::streamsize>(right_buffer.size()));
        if (left.gcount() != right.gcount() || !std::equal(
                left_buffer.begin(),
                left_buffer.begin() + left.gcount(),
                right_buffer.begin()
            )) {
            throw btrfsbackup::restore::RestoreError(
                btrfsbackup::restore::RestoreErrorCode::VerificationFailed,
                "restored file content differs: " + destination.string()
            );
        }
    }
    if (left.bad() || right.bad()) {
        throw btrfsbackup::restore::RestoreError(
            btrfsbackup::restore::RestoreErrorCode::VerificationFailed,
            "could not verify restored file: " + destination.string()
        );
    }
}

void preserve_metadata(const std::filesystem::path& source, const std::filesystem::path& destination) {
    const struct stat status = lstat_or_throw(source);
    if (::chmod(destination.c_str(), status.st_mode & 07777) != 0) {
        throw btrfsbackup::restore::RestoreError(
            btrfsbackup::restore::RestoreErrorCode::CopyFailed,
            "could not preserve mode for " + destination.string() + ": " + std::strerror(errno)
        );
    }
    if (::geteuid() == 0 && ::chown(destination.c_str(), status.st_uid, status.st_gid) != 0) {
        throw btrfsbackup::restore::RestoreError(
            btrfsbackup::restore::RestoreErrorCode::CopyFailed,
            "could not preserve ownership for " + destination.string() + ": " + std::strerror(errno)
        );
    }
    std::error_code time_error;
    std::filesystem::last_write_time(destination, std::filesystem::last_write_time(source, time_error), time_error);
    if (time_error) {
        throw btrfsbackup::restore::RestoreError(
            btrfsbackup::restore::RestoreErrorCode::CopyFailed,
            "could not preserve modification time for " + destination.string()
        );
    }
}

void copy_regular_file(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    CancellationToken& cancellation,
    btrfsbackup::restore::RestoreStatistics& statistics
) {
    std::ifstream input(source, std::ios::binary);
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!input || !output) {
        throw btrfsbackup::restore::RestoreError(
            btrfsbackup::restore::RestoreErrorCode::CopyFailed,
            "could not open restore file: " + source.string()
        );
    }
    std::array<char, 64 * 1024> buffer{};
    while (input) {
        throw_if_cancelled(cancellation);
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count > 0) {
            output.write(buffer.data(), count);
            statistics.bytes += static_cast<std::uint64_t>(count);
        }
    }
    output.flush();
    if (input.bad() || !output) {
        throw btrfsbackup::restore::RestoreError(
            btrfsbackup::restore::RestoreErrorCode::CopyFailed,
            "restore copy failed: " + source.string()
        );
    }
    preserve_metadata(source, destination);
    verify_regular_file(source, destination, cancellation);
    ++statistics.files;
}

void copy_entry(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    const SourceIdentity& identity,
    CancellationToken& cancellation,
    btrfsbackup::restore::RestoreStatistics& statistics
) {
    throw_if_cancelled(cancellation);
    const struct stat status = lstat_or_throw(source);
    if (status.st_dev != identity.device) {
        throw btrfsbackup::restore::RestoreError(
            btrfsbackup::restore::RestoreErrorCode::MountBoundaryRejected,
            "restore source crosses a mount boundary: " + source.string()
        );
    }
    if (S_ISLNK(status.st_mode)) {
        throw btrfsbackup::restore::RestoreError(
            btrfsbackup::restore::RestoreErrorCode::SymlinkRejected,
            "symbolic links are not restored: " + source.string()
        );
    }
    if (S_ISREG(status.st_mode)) {
        copy_regular_file(source, destination, cancellation, statistics);
        return;
    }
    if (!S_ISDIR(status.st_mode)) {
        throw btrfsbackup::restore::RestoreError(
            btrfsbackup::restore::RestoreErrorCode::PathInvalid,
            "unsupported restore entry type: " + source.string()
        );
    }
    std::error_code create_error;
    std::filesystem::create_directory(destination, create_error);
    if (create_error && create_error != std::errc::file_exists) {
        throw btrfsbackup::restore::RestoreError(
            btrfsbackup::restore::RestoreErrorCode::CopyFailed,
            "could not create restore directory: " + destination.string()
        );
    }
    ++statistics.directories;
    for (const std::filesystem::directory_entry& child : std::filesystem::directory_iterator(source)) {
        copy_entry(child.path(), destination / child.path().filename(), identity, cancellation, statistics);
    }
    preserve_metadata(source, destination);
}

bool is_subvolume(const std::filesystem::path& path) {
    return btrfs_util_subvolume_is_valid(path.c_str()) == BTRFS_UTIL_OK;
}

} // namespace

bool PosixRestoreOperations::exists(const std::filesystem::path& path) const {
    std::error_code error;
    return std::filesystem::exists(std::filesystem::symlink_status(path, error));
}

void PosixRestoreOperations::prepare_copy_root(
    const std::filesystem::path& source,
    const std::filesystem::path& path
) {
    const struct stat status = lstat_or_throw(source);
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        throw btrfsbackup::restore::RestoreError(
            btrfsbackup::restore::RestoreErrorCode::DestinationUnsafe,
            "could not create restore parent: " + path.parent_path().string()
        );
    }
    if (S_ISDIR(status.st_mode)) {
        std::filesystem::create_directory(path, error);
        if (error) {
            throw btrfsbackup::restore::RestoreError(
                btrfsbackup::restore::RestoreErrorCode::DestinationUnsafe,
                "could not create restore staging directory: " + path.string()
            );
        }
    }
}

void PosixRestoreOperations::create_subvolume_root(const std::filesystem::path& path) {
    std::error_code parent_error;
    std::filesystem::create_directories(path.parent_path(), parent_error);
    if (parent_error) {
        throw btrfsbackup::restore::RestoreError(
            btrfsbackup::restore::RestoreErrorCode::DestinationUnsafe,
            "could not create subvolume parent: " + path.parent_path().string()
        );
    }
    const enum btrfs_util_error error = btrfs_util_subvolume_create(path.c_str(), 0, nullptr, nullptr);
    if (error != BTRFS_UTIL_OK) {
        throw btrfsbackup::restore::RestoreError(
            btrfsbackup::restore::RestoreErrorCode::CopyFailed,
            "could not create restore subvolume " + path.string() + ": " + btrfs_util_strerror(error)
        );
    }
}

btrfsbackup::restore::RestoreStatistics PosixRestoreOperations::copy_and_verify(
    const std::filesystem::path& source,
    const std::filesystem::path& destination_root,
    CancellationToken& cancellation
) {
    const struct stat source_status = lstat_or_throw(source);
    btrfsbackup::restore::RestoreStatistics statistics;
    if (S_ISDIR(source_status.st_mode)) {
        for (const std::filesystem::directory_entry& child : std::filesystem::directory_iterator(source)) {
            copy_entry(
                child.path(),
                destination_root / child.path().filename(),
                SourceIdentity{source_status.st_dev},
                cancellation,
                statistics
            );
        }
        preserve_metadata(source, destination_root);
    } else {
        copy_entry(source, destination_root, SourceIdentity{source_status.st_dev}, cancellation, statistics);
    }
    return statistics;
}

void PosixRestoreOperations::move(const std::filesystem::path& source, const std::filesystem::path& destination) {
    std::error_code error;
    std::filesystem::rename(source, destination, error);
    if (error) {
        throw btrfsbackup::restore::RestoreError(
            btrfsbackup::restore::RestoreErrorCode::CopyFailed,
            "could not commit restore transaction: " + error.message()
        );
    }
}

void PosixRestoreOperations::remove_owned_tree(const std::filesystem::path& path) {
    if (!exists(path)) {
        return;
    }
    if (is_subvolume(path)) {
        for (const std::filesystem::directory_entry& child : std::filesystem::directory_iterator(path)) {
            std::error_code child_error;
            std::filesystem::remove_all(child.path(), child_error);
            if (child_error) {
                throw btrfsbackup::restore::RestoreError(
                    btrfsbackup::restore::RestoreErrorCode::CleanupIncomplete,
                    "could not clean restore subvolume: " + child_error.message()
                );
            }
        }
        const enum btrfs_util_error error = btrfs_util_subvolume_delete(path.c_str(), 0);
        if (error != BTRFS_UTIL_OK) {
            throw btrfsbackup::restore::RestoreError(
                btrfsbackup::restore::RestoreErrorCode::CleanupIncomplete,
                "could not delete restore subvolume: " + std::string(btrfs_util_strerror(error))
            );
        }
        return;
    }
    std::error_code error;
    std::filesystem::remove_all(path, error);
    if (error) {
        throw btrfsbackup::restore::RestoreError(
            btrfsbackup::restore::RestoreErrorCode::CleanupIncomplete,
            "could not clean restore staging: " + error.message()
        );
    }
}

} // namespace btrfsbackup::platform::linux::restore
