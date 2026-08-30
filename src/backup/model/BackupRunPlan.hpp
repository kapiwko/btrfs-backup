// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <backup/model/BackupPlanningSnapshot.hpp>
#include <backup/model/BackupRunActions.hpp>
#include <config/model/Profile.hpp>
#include <core/Identifiers.hpp>
#include <backup/model/SnapshotInventory.hpp>

namespace btrfsbackup::backup {

class BackupSourceRunPlan {
  public:
    explicit BackupSourceRunPlan(SourceId source_id, std::vector<BackupRunAction> actions = {});

    [[nodiscard]] const std::vector<BackupRunAction>& actions() const noexcept;

    const SourceId source_id;

  private:
    std::vector<BackupRunAction> actions_;
};

struct BackupRunPlan {
    ProfileId profile_id;
    RunId run_id;
    std::filesystem::path target_mount_point;
    std::vector<BackupSourceRunPlan> sources;
};

[[nodiscard]] BackupRunPlan build_backup_run_plan(
    const btrfsbackup::config::Profile& profile,
    const SnapshotInventoryBySource& local_inventory,
    const SnapshotInventoryBySource& remote_inventory,
    const PendingMarkerBySource& pending_markers,
    const PendingSnapshotBySource& pending_snapshots,
    const std::filesystem::path& profile_state_dir,
    const RunId& run_id,
    RuntimeTimePoint snapshot_timestamp
);

} // namespace btrfsbackup::backup
