// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <vector>

#include <backup/model/backup_run_plan.hpp>
#include <backup/ports/mount_inspector.hpp>
#include <config/application_paths.hpp>
#include <config/model/profile.hpp>

namespace btrfsbackup::backup {

struct BackupDiscoveryResult {
    SnapshotInventoryBySource local_inventory;
    SnapshotInventoryBySource remote_inventory;
    PendingMarkerBySource pending_markers;
    PendingSnapshotBySource pending_snapshots;
    std::filesystem::path profile_state_dir;
};

class IBackupDiscovery {
  public:
    virtual ~IBackupDiscovery() = default;

    [[nodiscard]] virtual BackupDiscoveryResult discover(
        const btrfsbackup::config::Profile& profile,
        const std::vector<MountEntry>& mounts,
        const btrfsbackup::config::ApplicationPaths& paths
    ) const = 0;
};

} // namespace btrfsbackup::backup
