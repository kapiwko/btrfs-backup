// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/backup_planner.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <backup/model/pending_recovery.hpp>
#include <backup/target_mount_validation.hpp>
#include <config/model/validation.hpp>
#include <core/errors.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

BackupPlanner::BackupPlanner(
    SnapshotMetadataReader metadata_reader,
    const IPendingMarkerStore& pending_markers,
    const ISafeDirectoryRootFactory& safe_directories
)
    : metadata_reader_(std::move(metadata_reader)),
      pending_markers_(pending_markers),
      safe_directories_(safe_directories) {
}

BackupRunPlan BackupPlanner::build(
    const Profile& profile,
    const std::vector<MountEntry>& mounts,
    const ApplicationPaths& paths,
    const RunId& run_id,
    const std::string& snapshot_timestamp
) const {
    validate_target_mount(profile, mounts);
    for (const ProfileSource& source : profile.sources) {
        if (!source.enabled) {
            continue;
        }
        if (!paths_are_same_filesystem(mounts, source.subvolume, source.local_snapshot_dir)) {
            throw ValidationError("LOCAL_SNAPSHOT_DIR must be on the same Btrfs filesystem as " + source.subvolume);
        }
        if (paths_are_same_filesystem(mounts, source.subvolume, profile.target.mount_point)) {
            throw ValidationError("SOURCE_SUBVOLUME must not be on the backup target filesystem: " + source.subvolume);
        }
        if (path_is_within(source.local_snapshot_dir, profile.target.mount_point)) {
            throw ValidationError("LOCAL_SNAPSHOT_DIR must not be inside the backup target: " + source.local_snapshot_dir);
        }
    }
    SnapshotInventoryBySource local_inventory;
    SnapshotInventoryBySource remote_inventory;
    PendingMarkerBySource pending_markers;
    PendingSnapshotBySource pending_snapshots;
    const fs::path profile_state = profile_state_dir(paths, std::string(profile.id.value()));
    std::unique_ptr<ISafeDirectoryRoot> local_root = safe_directories_.open("/");
    std::unique_ptr<ISafeDirectoryRoot> target_root = safe_directories_.open(profile.target.mount_point);

    for (const ProfileSource& source : profile.sources) {
        if (!source.enabled) {
            continue;
        }
        const std::string source_id{source.id.value()};
        const fs::path remote_dir = fs::path(profile.paths.remote_root) / source.remote_subdir;
        if (local_root->exists(source.local_snapshot_dir)) {
            std::unique_ptr<ISafeDirectoryHandle> local = local_root->pin_directory(source.local_snapshot_dir);
            local_inventory[source_id] = list_snapshot_inventory_at(
                local->stable_path(),
                source.local_snapshot_dir,
                source_id,
                SnapshotSide::Local,
                [&](const fs::path& path) {
                    std::unique_ptr<ISafeDirectoryHandle> snapshot = local_root->pin_directory(
                        fs::path(source.local_snapshot_dir) / path.filename()
                    );
                    return metadata_reader_(snapshot->stable_path());
                }
            );
        }
        if (target_root->exists(remote_dir)) {
            std::unique_ptr<ISafeDirectoryHandle> remote = target_root->pin_directory(remote_dir);
            remote_inventory[source_id] = list_snapshot_inventory_at(
                remote->stable_path(),
                remote_dir,
                source_id,
                SnapshotSide::Remote,
                [&](const fs::path& path) {
                    std::unique_ptr<ISafeDirectoryHandle> snapshot = target_root->pin_directory(
                        remote_dir / path.filename()
                    );
                    return metadata_reader_(snapshot->stable_path());
                }
            );
        }

        const std::optional<PendingMarker> marker = pending_markers_.read(profile_state, source_id);
        pending_markers[source_id] = marker;
        if (marker.has_value()) {
            if (local_root->exists(marker->local_snapshot_path)) {
                std::unique_ptr<ISafeDirectoryHandle> snapshot = local_root->pin_directory(
                    marker->local_snapshot_path
                );
                pending_snapshots[source_id] = metadata_reader_(snapshot->stable_path());
            } else {
                pending_snapshots[source_id] = std::nullopt;
            }
        }
    }

    return build_backup_run_plan(
        profile,
        local_inventory,
        remote_inventory,
        pending_markers,
        pending_snapshots,
        profile_state,
        run_id,
        snapshot_timestamp
    );
}

} // namespace btrfsbackup
