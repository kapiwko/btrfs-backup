// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <backup/model/backup_run_actions.hpp>
#include <backup/model/incremental_parent.hpp>
#include <backup/model/pending_recovery.hpp>
#include <config/model/profile.hpp>
#include <core/identifiers.hpp>
#include <backup/model/retention_plan.hpp>
#include <backup/model/snapshot_inventory.hpp>

namespace btrfsbackup::backup {

struct BackupSourceRunPlan {
    SourceId source_id;
    std::filesystem::path source_subvolume;
    std::filesystem::path local_snapshot_dir;
    std::filesystem::path remote_snapshot_dir;
    std::filesystem::path incoming_source_root;
    std::filesystem::path incoming_run_dir;
    std::filesystem::path local_snapshot_path;
    std::filesystem::path received_snapshot_path;
    std::filesystem::path final_remote_snapshot_path;
    IncrementalParentSelection parent;
    PendingRecoveryPlan recovery;
    RetentionPlan local_retention;
    RetentionPlan remote_retention;
    std::vector<BackupRunAction> actions;
};

struct BackupRunPlan {
    ProfileId profile_id;
    RunId run_id;
    std::filesystem::path target_mount_point;
    std::vector<BackupSourceRunPlan> sources;
};

using SnapshotInventoryBySource = std::map<std::string, std::vector<SnapshotInfo>>;
using PendingMarkerBySource = std::map<std::string, std::optional<PendingMarker>>;
using PendingSnapshotBySource = std::map<std::string, std::optional<SnapshotMetadata>>;

[[nodiscard]] BackupRunPlan build_backup_run_plan(
    const btrfsbackup::config::Profile& profile,
    const SnapshotInventoryBySource& local_inventory,
    const SnapshotInventoryBySource& remote_inventory,
    const PendingMarkerBySource& pending_markers,
    const PendingSnapshotBySource& pending_snapshots,
    const std::filesystem::path& profile_state_dir,
    const RunId& run_id,
    const std::string& snapshot_timestamp
);

} // namespace btrfsbackup::backup
