// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/backup_discovery.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <backup/model/pending_recovery.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::backup {

BackupDiscovery::BackupDiscovery(
    SnapshotMetadataReader metadata_reader,
    const IPendingMarkerStore& pending_markers,
    const ISafeDirectoryRootFactory& safe_directories
)
    : metadata_reader_(std::move(metadata_reader)),
      pending_markers_(pending_markers),
      safe_directories_(safe_directories) {
}

BackupPlanningSnapshot BackupDiscovery::discover(
    const btrfsbackup::config::Profile& profile,
    const btrfsbackup::config::ApplicationPaths& paths
) const {
    SnapshotInventoryBySource local_inventory;
    SnapshotInventoryBySource remote_inventory;
    PendingMarkerBySource pending_markers;
    PendingSnapshotBySource pending_snapshots;
    const fs::path profile_state = btrfsbackup::config::profile_state_dir(paths, std::string(profile.id.value()));
    std::unique_ptr<ISafeDirectoryRoot> local_root = safe_directories_.open("/");
    std::unique_ptr<ISafeDirectoryRoot> target_root = safe_directories_.open(profile.target.mount_point);

    for (const btrfsbackup::config::ProfileSource& source : profile.sources) {
        if (!source.enabled) {
            continue;
        }
        const SourceId& source_id = source.id;
        const std::string source_id_value{source_id.value()};
        const fs::path remote_dir = profile.paths.remote_root.value() / source.remote_subdir.value();
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

        const std::optional<PendingMarker> marker = pending_markers_.read(profile_state, source_id_value);
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

    return BackupPlanningSnapshot{
        std::move(local_inventory),
        std::move(remote_inventory),
        std::move(pending_markers),
        std::move(pending_snapshots),
        profile_state,
    };
}

} // namespace btrfsbackup::backup
