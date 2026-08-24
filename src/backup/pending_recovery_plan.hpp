#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <state/run_state.hpp>
#include <backup/snapshot_inventory.hpp>

namespace btrfsbackup {

enum class PendingRecoveryAction {
    NoMarker,
    ClearInvalidMarker,
    ClearMissingSnapshot,
    PreserveCommittedSnapshot,
    DeleteInvalidCommittedSnapshot,
    KeepFailedLocalSnapshot,
    DeleteOrphanSnapshot,
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

std::optional<PendingMarker> read_pending_marker_if_exists(
    const std::filesystem::path& profile_state_dir,
    const std::string& source_id
);

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

} // namespace btrfsbackup
