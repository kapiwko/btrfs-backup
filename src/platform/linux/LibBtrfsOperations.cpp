// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <platform/linux/LibBtrfsOperations.hpp>

#include <array>
#include <cstdio>
#include <filesystem>
#include <optional>

#include <btrfsutil.h>

#include <core/Errors.hpp>
#include <backup/model/SnapshotInventory.hpp>

namespace fs = std::filesystem;

namespace {

[[noreturn]] void throw_btrfs_error(const std::string& operation, const fs::path& path, enum btrfs_util_error error) {
    throw btrfsbackup::ValidationError(operation + " failed for " + path.string() + ": " + btrfs_util_strerror(error));
}

bool is_zero_uuid(const uint8_t uuid[16]) {
    for (int index = 0; index < 16; ++index) {
        if (uuid[index] != 0) {
            return false;
        }
    }
    return true;
}

std::string uuid_to_string(const uint8_t uuid[16]) {
    std::array<char, 37> buffer{};
    std::snprintf(
        buffer.data(),
        buffer.size(),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        uuid[0],
        uuid[1],
        uuid[2],
        uuid[3],
        uuid[4],
        uuid[5],
        uuid[6],
        uuid[7],
        uuid[8],
        uuid[9],
        uuid[10],
        uuid[11],
        uuid[12],
        uuid[13],
        uuid[14],
        uuid[15]
    );
    return buffer.data();
}

} // namespace

namespace btrfsbackup::platform::linux {

std::optional<btrfsbackup::backup::SnapshotMetadata> read_btrfs_snapshot_metadata(const fs::path& path) {
    struct btrfs_util_subvolume_info info{};
    enum btrfs_util_error info_error = btrfs_util_subvolume_get_info(path.c_str(), 0, &info);
    if (info_error == BTRFS_UTIL_ERROR_NOT_BTRFS || info_error == BTRFS_UTIL_ERROR_NOT_SUBVOLUME || info_error == BTRFS_UTIL_ERROR_SUBVOLUME_NOT_FOUND) {
        return std::nullopt;
    }
    if (info_error != BTRFS_UTIL_OK) {
        throw ValidationError("could not read Btrfs subvolume info for " + path.string() + ": " + btrfs_util_strerror(info_error));
    }

    btrfsbackup::backup::SnapshotMetadata metadata;
    metadata.is_subvolume = true;
    metadata.uuid = uuid_to_string(info.uuid);
    if (!is_zero_uuid(info.received_uuid)) {
        metadata.received_uuid = uuid_to_string(info.received_uuid);
    }

    bool readonly = false;
    enum btrfs_util_error readonly_error = btrfs_util_subvolume_get_read_only(path.c_str(), &readonly);
    if (readonly_error != BTRFS_UTIL_OK) {
        throw ValidationError("could not read Btrfs subvolume readonly flag for " + path.string() + ": " + btrfs_util_strerror(readonly_error));
    }
    metadata.readonly = readonly;

    return metadata;
}

bool LibBtrfsOperations::is_subvolume(const fs::path& path) {
    enum btrfs_util_error error = btrfs_util_subvolume_is_valid(path.c_str());
    if (error == BTRFS_UTIL_OK) {
        return true;
    }
    if (error == BTRFS_UTIL_ERROR_NOT_BTRFS || error == BTRFS_UTIL_ERROR_NOT_SUBVOLUME || error == BTRFS_UTIL_ERROR_SUBVOLUME_NOT_FOUND) {
        return false;
    }
    throw_btrfs_error("Btrfs subvolume check", path, error);
}

std::optional<btrfsbackup::backup::SnapshotMetadata> LibBtrfsOperations::read_snapshot_metadata(const fs::path& path) {
    return read_btrfs_snapshot_metadata(path);
}

void LibBtrfsOperations::create_readonly_snapshot(const fs::path& source, const fs::path& target) {
    enum btrfs_util_error error = btrfs_util_subvolume_snapshot(
        source.c_str(),
        target.c_str(),
        BTRFS_UTIL_CREATE_SNAPSHOT_READ_ONLY,
        nullptr,
        nullptr
    );
    if (error != BTRFS_UTIL_OK) {
        throw_btrfs_error("Btrfs readonly snapshot creation", target, error);
    }
}

void LibBtrfsOperations::delete_subvolume(const fs::path& path) {
    enum btrfs_util_error error = btrfs_util_subvolume_delete(path.c_str(), 0);
    if (error != BTRFS_UTIL_OK) {
        throw_btrfs_error("Btrfs subvolume deletion", path, error);
    }
}

bool LibBtrfsOperations::is_subvolume_beneath(const btrfsbackup::backup::ISafeDirectoryRoot& root, const fs::path& path) {
    std::unique_ptr<btrfsbackup::backup::ISafeDirectoryHandle> handle = root.pin_directory(path);
    return is_subvolume(handle->stable_path());
}

std::optional<btrfsbackup::backup::SnapshotMetadata> LibBtrfsOperations::read_snapshot_metadata_beneath(
    const btrfsbackup::backup::ISafeDirectoryRoot& root,
    const fs::path& path
) {
    std::unique_ptr<btrfsbackup::backup::ISafeDirectoryHandle> handle = root.pin_directory(path);
    return read_snapshot_metadata(handle->stable_path());
}

void LibBtrfsOperations::create_readonly_snapshot_beneath(
    const btrfsbackup::backup::ISafeDirectoryRoot& source_root,
    const fs::path& source,
    const btrfsbackup::backup::ISafeDirectoryRoot& target_root,
    const fs::path& target
) {
    std::unique_ptr<btrfsbackup::backup::ISafeDirectoryHandle> source_handle = source_root.pin_directory(source);
    target_root.ensure_directory(target.parent_path());
    if (target_root.exists(target)) {
        throw ValidationError("snapshot target already exists: " + target.string());
    }
    std::unique_ptr<btrfsbackup::backup::ISafeDirectoryHandle> target_parent = target_root.pin_directory(target.parent_path());
    create_readonly_snapshot(source_handle->stable_path(), target_parent->stable_path() / target.filename());
}

void LibBtrfsOperations::delete_subvolume_beneath(const btrfsbackup::backup::ISafeDirectoryRoot& root, const fs::path& path) {
    root.delete_subvolume(path);
}

} // namespace btrfsbackup::platform::linux
