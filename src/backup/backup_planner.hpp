// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/model/snapshot_inventory.hpp>
#include <backup/ports/backup_planner.hpp>
#include <backup/ports/pending_marker_store.hpp>
#include <backup/ports/safe_directory.hpp>

namespace btrfsbackup {

class BackupPlanner final : public IBackupPlanner {
  public:
    explicit BackupPlanner(
        SnapshotMetadataReader metadata_reader,
        const IPendingMarkerStore& pending_markers,
        const ISafeDirectoryRootFactory& safe_directories
    );

    [[nodiscard]] BackupRunPlan build(
        const Profile& profile,
        const std::vector<MountEntry>& mounts,
        const ApplicationPaths& paths,
        const RunId& run_id,
        const std::string& snapshot_timestamp
    ) const override;

  private:
    SnapshotMetadataReader metadata_reader_;
    const IPendingMarkerStore& pending_markers_;
    const ISafeDirectoryRootFactory& safe_directories_;
};

} // namespace btrfsbackup
