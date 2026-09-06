// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/restore/PosixRestoreOperations.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>
#include <vector>

#include <btrfsutil.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <restore/RestoreError.hpp>
#include <platform/linux/OwnedFileDescriptor.hpp>

namespace btrfsbackup::platform::linux::restore {

namespace {

struct SourceIdentity {
    dev_t device = 0;
};

bool is_pinned_descriptor_component(const std::filesystem::path& path) {
    const std::filesystem::path prefix{"/proc/self/fd"};
    const auto relative = path.lexically_relative(prefix);
    if (relative.empty() || relative.is_absolute())
        return false;
    const std::string descriptor_text = (*relative.begin()).string();
    if (descriptor_text.empty() || !std::ranges::all_of(descriptor_text, [](unsigned char value) {
            return std::isdigit(value) != 0;
        }))
        return false;
    try {
        const int descriptor = std::stoi(descriptor_text);
        struct stat status{};
        return descriptor >= 0 && ::fstat(descriptor, &status) == 0 &&
            (S_ISDIR(status.st_mode) || S_ISREG(status.st_mode));
    } catch (...) {
        return false;
    }
}

void reject_symlink_components(const std::filesystem::path& path, bool leaf_may_be_missing) {
    const bool pinned = is_pinned_descriptor_component(path);
    std::filesystem::path current;
    for (const std::filesystem::path& component : path) {
        current /= component;
        struct stat status{};
        if (::lstat(current.c_str(), &status) != 0) {
            if (leaf_may_be_missing && errno == ENOENT) {
                return;
            }
            throw btrfsbackup::restore::RestoreError(
                btrfsbackup::restore::RestoreErrorCode::DestinationUnsafe,
                "could not inspect restore path " + current.string() + ": " + std::strerror(errno)
            );
        }
        if (S_ISLNK(status.st_mode) &&
            !(pinned && (current == "/proc/self" || current.parent_path() == "/proc/self/fd"))) {
            throw btrfsbackup::restore::RestoreError(
                btrfsbackup::restore::RestoreErrorCode::SymlinkRejected,
                "restore path traverses a symbolic link: " + current.string()
            );
        }
    }
}

struct stat lstat_or_throw(const std::filesystem::path& path) {
    struct stat status{};
    const int result = path.parent_path() == "/proc/self/fd"
        ? ::stat(path.c_str(), &status)
        : ::lstat(path.c_str(), &status);
    if (result != 0) {
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

[[noreturn]] void throw_copy_failure(
    const std::string& message,
    int error_number = errno
) {
    if (error_number == 0)
        error_number = EIO;
    throw btrfsbackup::restore::RestoreError(
        error_number == ENOSPC
            ? btrfsbackup::restore::RestoreErrorCode::InsufficientSpace
            : btrfsbackup::restore::RestoreErrorCode::CopyFailed,
        message + ": " + std::strerror(error_number)
    );
}

std::uint64_t add_required_bytes(std::uint64_t total, std::uint64_t amount) {
    if (amount > std::numeric_limits<std::uint64_t>::max() - total)
        return std::numeric_limits<std::uint64_t>::max();
    return total + amount;
}

std::uint64_t required_restore_bytes(
    const std::filesystem::path& source,
    const SourceIdentity& identity,
    CancellationToken& cancellation
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
    if (S_ISREG(status.st_mode))
        return status.st_size < 0 ? 0 : static_cast<std::uint64_t>(status.st_size);
    if (!S_ISDIR(status.st_mode)) {
        throw btrfsbackup::restore::RestoreError(
            btrfsbackup::restore::RestoreErrorCode::PathInvalid,
            "unsupported restore entry type: " + source.string()
        );
    }

    std::uint64_t total = 0;
    for (const std::filesystem::directory_entry& child : std::filesystem::directory_iterator(source))
        total = add_required_bytes(total, required_restore_bytes(child.path(), identity, cancellation));
    return total;
}

std::filesystem::path existing_destination_ancestor(std::filesystem::path path) {
    std::error_code error;
    while (!path.empty() && !std::filesystem::exists(path, error)) {
        if (error) {
            throw btrfsbackup::restore::RestoreError(
                btrfsbackup::restore::RestoreErrorCode::DestinationUnsafe,
                "could not inspect restore destination: " + error.message()
            );
        }
        const std::filesystem::path parent = path.parent_path();
        if (parent == path)
            break;
        path = parent;
    }
    return path.empty() ? std::filesystem::path{"/"} : path;
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
        if (left.gcount() != right.gcount() || !std::equal(left_buffer.begin(), left_buffer.begin() + left.gcount(), right_buffer.begin())) {
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

void copy_extended_attributes(const std::filesystem::path& source, const std::filesystem::path& destination) {
    const ssize_t names_size = ::listxattr(source.c_str(), nullptr, 0);
    if (names_size < 0) {
        throw btrfsbackup::restore::RestoreError(
            btrfsbackup::restore::RestoreErrorCode::CopyFailed,
            "could not list extended attributes for " + source.string() + ": " + std::strerror(errno)
        );
    }
    std::vector<char> names(static_cast<std::size_t>(names_size));
    if (names_size > 0 && ::listxattr(source.c_str(), names.data(), names.size()) != names_size) {
        throw btrfsbackup::restore::RestoreError(
            btrfsbackup::restore::RestoreErrorCode::CopyFailed,
            "extended attributes changed while restoring: " + source.string()
        );
    }
    std::size_t offset = 0;
    while (offset < names.size()) {
        const std::string name{names.data() + offset};
        offset += name.size() + 1;
        const ssize_t value_size = ::getxattr(source.c_str(), name.c_str(), nullptr, 0);
        if (value_size < 0) {
            throw btrfsbackup::restore::RestoreError(
                btrfsbackup::restore::RestoreErrorCode::CopyFailed,
                "could not read extended attribute " + name
            );
        }
        std::vector<char> value(static_cast<std::size_t>(value_size));
        if (value_size > 0 && ::getxattr(source.c_str(), name.c_str(), value.data(), value.size()) != value_size) {
            throw btrfsbackup::restore::RestoreError(
                btrfsbackup::restore::RestoreErrorCode::CopyFailed,
                "extended attribute changed while restoring: " + name
            );
        }
        if (::setxattr(destination.c_str(), name.c_str(), value.data(), value.size(), 0) != 0)
            throw_copy_failure("could not preserve extended attribute " + name);
    }
}

void preserve_metadata(const std::filesystem::path& source, const std::filesystem::path& destination) {
    const struct stat status = lstat_or_throw(source);
    copy_extended_attributes(source, destination);
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
    const struct stat restored = lstat_or_throw(destination);
    if ((restored.st_mode & 07777) != (status.st_mode & 07777) ||
        (::geteuid() == 0 && (restored.st_uid != status.st_uid || restored.st_gid != status.st_gid))) {
        throw btrfsbackup::restore::RestoreError(
            btrfsbackup::restore::RestoreErrorCode::VerificationFailed,
            "restored metadata differs: " + destination.string()
        );
    }
}

void copy_regular_file(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    CancellationToken& cancellation,
    btrfsbackup::restore::RestoreStatistics& statistics,
    const btrfsbackup::restore::RestoreProgressSink& progress
) {
    btrfsbackup::platform::linux::OwnedFileDescriptor input{
        ::open(source.c_str(), O_RDONLY | O_CLOEXEC)
    };
    if (!input.valid())
        throw_copy_failure("could not open restore file " + source.string());
    btrfsbackup::platform::linux::OwnedFileDescriptor output{
        ::open(destination.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600)
    };
    if (!output.valid())
        throw_copy_failure("could not create restore file " + destination.string());

    std::array<char, 64 * 1024> buffer{};
    while (true) {
        throw_if_cancelled(cancellation);
        ssize_t count = ::read(input.get(), buffer.data(), buffer.size());
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0)
            throw_copy_failure("could not read restore file " + source.string());
        if (count == 0)
            break;

        ssize_t written = 0;
        while (written < count) {
            const ssize_t result = ::write(
                output.get(),
                buffer.data() + written,
                static_cast<std::size_t>(count - written)
            );
            if (result < 0 && errno == EINTR)
                continue;
            if (result < 0)
                throw_copy_failure("could not write restore file " + destination.string());
            if (result == 0)
                throw_copy_failure("could not write restore file " + destination.string(), EIO);
            written += result;
        }
        statistics.bytes += static_cast<std::uint64_t>(count);
        if (progress)
            progress({statistics, source});
    }
    if (::fsync(output.get()) != 0)
        throw_copy_failure("could not synchronize restore file " + destination.string());
    const int output_descriptor = output.release();
    if (::close(output_descriptor) != 0)
        throw_copy_failure("could not close restore file " + destination.string());
    preserve_metadata(source, destination);
    verify_regular_file(source, destination, cancellation);
    ++statistics.files;
    if (progress)
        progress({statistics, source});
}

void copy_entry(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    const SourceIdentity& identity,
    CancellationToken& cancellation,
    btrfsbackup::restore::RestoreStatistics& statistics,
    const btrfsbackup::restore::RestoreProgressSink& progress
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
        copy_regular_file(source, destination, cancellation, statistics, progress);
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
            create_error == std::errc::no_space_on_device
                ? btrfsbackup::restore::RestoreErrorCode::InsufficientSpace
                : btrfsbackup::restore::RestoreErrorCode::CopyFailed,
            "could not create restore directory " + destination.string() + ": " + create_error.message()
        );
    }
    ++statistics.directories;
    if (progress)
        progress({statistics, source});
    for (const std::filesystem::directory_entry& child : std::filesystem::directory_iterator(source)) {
        copy_entry(child.path(), destination / child.path().filename(), identity, cancellation, statistics, progress);
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

void PosixRestoreOperations::ensure_sufficient_space(
    const std::filesystem::path& source,
    const std::filesystem::path& destination,
    CancellationToken& cancellation
) const {
    reject_symlink_components(source, false);
    reject_symlink_components(destination.parent_path(), true);
    const struct stat source_status = lstat_or_throw(source);
    const std::uint64_t required = required_restore_bytes(
        source,
        SourceIdentity{source_status.st_dev},
        cancellation
    );
    const std::filesystem::path ancestor = existing_destination_ancestor(destination.parent_path());
    std::error_code error;
    const std::filesystem::space_info space = std::filesystem::space(ancestor, error);
    if (error) {
        throw btrfsbackup::restore::RestoreError(
            btrfsbackup::restore::RestoreErrorCode::DestinationUnsafe,
            "could not inspect free space at restore destination: " + error.message()
        );
    }
    if (required > space.available) {
        throw btrfsbackup::restore::RestoreError(
            btrfsbackup::restore::RestoreErrorCode::InsufficientSpace,
            "restore requires " + std::to_string(required) + " bytes but only " +
                std::to_string(space.available) + " bytes are available"
        );
    }
}

void PosixRestoreOperations::prepare_copy_root(
    const std::filesystem::path& source,
    const std::filesystem::path& path
) {
    reject_symlink_components(source, false);
    reject_symlink_components(path.parent_path(), true);
    const struct stat status = lstat_or_throw(source);
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) {
        throw btrfsbackup::restore::RestoreError(
            error == std::errc::no_space_on_device
                ? btrfsbackup::restore::RestoreErrorCode::InsufficientSpace
                : btrfsbackup::restore::RestoreErrorCode::DestinationUnsafe,
            "could not create restore parent: " + path.parent_path().string()
        );
    }
    reject_symlink_components(path.parent_path(), false);
    if (S_ISDIR(status.st_mode)) {
        std::filesystem::create_directory(path, error);
        if (error) {
            throw btrfsbackup::restore::RestoreError(
                error == std::errc::no_space_on_device
                    ? btrfsbackup::restore::RestoreErrorCode::InsufficientSpace
                    : btrfsbackup::restore::RestoreErrorCode::DestinationUnsafe,
                "could not create restore staging directory: " + path.string()
            );
        }
    }
}

void PosixRestoreOperations::create_subvolume_root(const std::filesystem::path& path) {
    reject_symlink_components(path.parent_path(), true);
    std::error_code parent_error;
    std::filesystem::create_directories(path.parent_path(), parent_error);
    if (parent_error) {
        throw btrfsbackup::restore::RestoreError(
            parent_error == std::errc::no_space_on_device
                ? btrfsbackup::restore::RestoreErrorCode::InsufficientSpace
                : btrfsbackup::restore::RestoreErrorCode::DestinationUnsafe,
            "could not create subvolume parent " + path.parent_path().string() + ": " + parent_error.message()
        );
    }
    reject_symlink_components(path.parent_path(), false);
    errno = 0;
    const enum btrfs_util_error error = btrfs_util_subvolume_create(path.c_str(), 0, nullptr, nullptr);
    const int create_error = errno;
    if (error != BTRFS_UTIL_OK) {
        throw btrfsbackup::restore::RestoreError(
            create_error == ENOSPC
                ? btrfsbackup::restore::RestoreErrorCode::InsufficientSpace
                : btrfsbackup::restore::RestoreErrorCode::CopyFailed,
            "could not create restore subvolume " + path.string() + ": " + btrfs_util_strerror(error)
        );
    }
}

btrfsbackup::restore::RestoreStatistics PosixRestoreOperations::copy_and_verify(
    const std::filesystem::path& source,
    const std::filesystem::path& destination_root,
    CancellationToken& cancellation,
    const btrfsbackup::restore::RestoreProgressSink& progress
) {
    reject_symlink_components(source, false);
    reject_symlink_components(destination_root.parent_path(), false);
    const struct stat source_status = lstat_or_throw(source);
    btrfsbackup::restore::RestoreStatistics statistics;
    if (S_ISDIR(source_status.st_mode)) {
        for (const std::filesystem::directory_entry& child : std::filesystem::directory_iterator(source)) {
            copy_entry(
                child.path(),
                destination_root / child.path().filename(),
                SourceIdentity{source_status.st_dev},
                cancellation,
                statistics,
                progress
            );
        }
        preserve_metadata(source, destination_root);
    } else {
        copy_entry(source, destination_root, SourceIdentity{source_status.st_dev}, cancellation, statistics, progress);
    }
    return statistics;
}

void PosixRestoreOperations::move(const std::filesystem::path& source, const std::filesystem::path& destination) {
    reject_symlink_components(source, false);
    reject_symlink_components(destination.parent_path(), false);
    std::error_code error;
    std::filesystem::rename(source, destination, error);
    if (error) {
        throw btrfsbackup::restore::RestoreError(
            error == std::errc::no_space_on_device
                ? btrfsbackup::restore::RestoreErrorCode::InsufficientSpace
                : btrfsbackup::restore::RestoreErrorCode::CopyFailed,
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
