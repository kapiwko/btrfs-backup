// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/planning/BackupPreflightValidation.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string>

#include <core/Errors.hpp>
#include <config/domain/Validation.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::backup::planning {

namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool has_mount_option(const std::string& options, const std::string& option) {
    return ("," + options + ",").contains("," + option + ",");
}

fs::path resolved_path(const fs::path& path) {
    std::error_code ec;
    fs::path resolved = fs::weakly_canonical(path, ec);
    if (ec) {
        return btrfsbackup::config::normalized_path(path);
    }
    return btrfsbackup::config::normalized_path(resolved);
}

bool resolved_path_is_within(const fs::path& candidate, const fs::path& base) {
    return btrfsbackup::config::path_is_within(resolved_path(candidate), resolved_path(base));
}

} // namespace

MountEntry validate_backup_target_mount(const btrfsbackup::config::Profile& profile, const std::vector<MountEntry>& mounts) {
    std::optional<MountEntry> target_mount = mount_at(mounts, profile.target.mount_point);
    if (!target_mount.has_value()) {
        throw ValidationError("Backup target is not mounted at " + profile.target.mount_point.value().string());
    }

    if (target_mount->fstype != "btrfs") {
        throw ValidationError("Backup target is not a Btrfs filesystem: " + profile.target.mount_point.value().string());
    }

    fs::path mapper_path = fs::path("/dev/mapper") / profile.target.mapper_name.value();
    if (!mount_uses_mapper(mounts, profile.target.mount_point, mapper_path)) {
        throw ValidationError("The filesystem mounted at " + profile.target.mount_point.value().string() + " is not " + mapper_path.string());
    }

    if (!has_mount_option(target_mount->options, "rw")) {
        throw ValidationError("Backup target is not mounted read-write: " + profile.target.mount_point.value().string());
    }

    for (const char* option : {"nodev", "nosuid", "noexec", "nosymfollow"}) {
        if (!has_mount_option(target_mount->options, option)) {
            throw ValidationError(
                std::string("Backup target is missing required mount option ") + option + ": " + profile.target.mount_point.value().string()
            );
        }
    }

    if (target_mount->filesystem_uuid.empty() ||
        lower(target_mount->filesystem_uuid) != profile.target.btrfs_uuid.value()) {
        throw ValidationError("Btrfs UUID mismatch at " + profile.target.mount_point.value().string());
    }

    if (!resolved_path_is_within(profile.paths.remote_root.value(), profile.target.mount_point)) {
        throw ValidationError("REMOTE_ROOT escapes the backup mountpoint: " + profile.paths.remote_root.value().string());
    }
    if (!resolved_path_is_within(profile.paths.incoming_root.value(), profile.target.mount_point)) {
        throw ValidationError("INCOMING_ROOT escapes the backup mountpoint: " + profile.paths.incoming_root.value().string());
    }
    return *target_mount;
}

MountEntry validate_backup_mounts(const btrfsbackup::config::Profile& profile, const std::vector<MountEntry>& mounts) {
    MountEntry target_mount = validate_backup_target_mount(profile, mounts);
    for (const btrfsbackup::config::ProfileSource& source : profile.sources) {
        if (!source.enabled) {
            continue;
        }
        if (!paths_are_same_filesystem(mounts, source.subvolume, source.local_snapshot_dir)) {
            throw ValidationError("LOCAL_SNAPSHOT_DIR must be on the same Btrfs filesystem as " + source.subvolume.value().string());
        }
        if (paths_are_same_filesystem(mounts, source.subvolume, profile.target.mount_point)) {
            throw ValidationError("SOURCE_SUBVOLUME must not be on the backup target filesystem: " + source.subvolume.value().string());
        }
        if (btrfsbackup::config::path_is_within(source.local_snapshot_dir, profile.target.mount_point)) {
            throw ValidationError("LOCAL_SNAPSHOT_DIR must not be inside the backup target: " + source.local_snapshot_dir.value().string());
        }
    }
    return target_mount;
}

} // namespace btrfsbackup::backup::planning
