// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/default_backup_planner.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <backup/pending_recovery_plan.hpp>
#include <backup/target_mount_validation.hpp>
#include <state/run_state.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

DefaultBackupPlanner::DefaultBackupPlanner(
    SnapshotMetadataReader metadata_reader,
    const ISafeDirectoryRootFactory& safe_directories
)
    : metadata_reader_(std::move(metadata_reader)), safe_directories_(safe_directories) {
}

BackupRunPlan DefaultBackupPlanner::build(
    const Profile& profile,
    const std::vector<MountEntry>& mounts,
    const ApplicationPaths& paths,
    const RunId& run_id,
    const std::string& snapshot_timestamp
) const {
    validate_target_mount(profile, mounts);
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

        const std::optional<PendingMarker> marker = read_pending_marker_if_exists(profile_state, source_id);
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
        mounts,
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
