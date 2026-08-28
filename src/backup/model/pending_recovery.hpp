// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <backup/model/snapshot.hpp>

namespace btrfsbackup::backup {

enum class PendingRecoveryAction {
    NoMarker,
    ClearInvalidMarker,
    ClearMissingSnapshot,
    PreserveCommittedSnapshot,
    DeleteInvalidCommittedSnapshot,
    KeepFailedLocalSnapshot,
    DeleteOrphanSnapshot,
};

struct PendingMarker {
    std::string source_name;
    std::string local_snapshot_path;
    std::string final_snapshot_path;
    std::string run_id;
    std::string timestamp;
};

struct PendingRecoveryPlan {
    PendingRecoveryAction action = PendingRecoveryAction::NoMarker;
    bool clear_marker = false;
    bool delete_local_snapshot = false;
    bool delete_remote_snapshot = false;
    std::filesystem::path marker_path;
    std::filesystem::path local_snapshot_path;
    std::filesystem::path remote_snapshot_path;
    std::string message;
};

PendingRecoveryPlan plan_pending_recovery(
    const std::string& source_id,
    const std::filesystem::path& profile_state_dir,
    const std::filesystem::path& local_snapshot_dir,
    const std::filesystem::path& remote_snapshot_dir,
    const std::optional<PendingMarker>& marker,
    const std::optional<SnapshotMetadata>& pending_snapshot,
    const std::vector<SnapshotInfo>& remote_snapshots,
    bool keep_failed_local_snapshot
);

} // namespace btrfsbackup::backup
