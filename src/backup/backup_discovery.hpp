// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/model/snapshot_inventory.hpp>
#include <backup/ports/backup_discovery.hpp>
#include <backup/ports/pending_marker_store.hpp>
#include <backup/ports/safe_directory.hpp>

namespace btrfsbackup::backup {

class BackupDiscovery final : public IBackupDiscovery {
  public:
    explicit BackupDiscovery(
        SnapshotMetadataReader metadata_reader,
        const IPendingMarkerStore& pending_markers,
        const ISafeDirectoryRootFactory& safe_directories
    );

    [[nodiscard]] BackupPlanningSnapshot discover(
        const btrfsbackup::config::Profile& profile,
        const btrfsbackup::config::ApplicationPaths& paths
    ) const override;

  private:
    SnapshotMetadataReader metadata_reader_;
    const IPendingMarkerStore& pending_markers_;
    const ISafeDirectoryRootFactory& safe_directories_;
};

} // namespace btrfsbackup::backup
