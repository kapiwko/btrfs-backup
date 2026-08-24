#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <btrfsbackup/incremental_parent.hpp>
#include <btrfsbackup/mount_info.hpp>
#include <btrfsbackup/pending_recovery_plan.hpp>
#include <btrfsbackup/profile.hpp>
#include <btrfsbackup/retention_plan.hpp>
#include <btrfsbackup/run_state.hpp>
#include <btrfsbackup/snapshot_inventory.hpp>

namespace btrfsbackup {

enum class BackupRunActionKind {
    RecoverPending,
    CleanupIncoming,
    BeforeSnapshotHook,
    CreateSnapshot,
    AfterSnapshotHook,
    SelectParent,
    SendReceive,
    VerifyReceived,
    CommitReceived,
    ApplyRemoteRetention,
    ApplyLocalRetention,
    CleanupSource,
};

struct BackupRunAction {
    BackupRunActionKind kind = BackupRunActionKind::CleanupSource;
    std::string source_id;
    std::filesystem::path primary_path;
    std::filesystem::path secondary_path;
    ProfileHookCommand hook;
};

struct BackupSourceRunPlan {
    std::string source_id;
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
    std::string profile_id;
    std::string run_id;
    std::filesystem::path target_mount_point;
    std::vector<BackupSourceRunPlan> sources;
};

using SnapshotInventoryBySource = std::map<std::string, std::vector<SnapshotInfo>>;
using PendingMarkerBySource = std::map<std::string, std::optional<PendingMarker>>;
using PendingSnapshotBySource = std::map<std::string, std::optional<SnapshotMetadata>>;

BackupRunPlan build_backup_run_plan(
    const Profile& profile,
    const std::vector<MountEntry>& mounts,
    const SnapshotInventoryBySource& local_inventory,
    const SnapshotInventoryBySource& remote_inventory,
    const PendingMarkerBySource& pending_markers,
    const PendingSnapshotBySource& pending_snapshots,
    const std::filesystem::path& profile_state_dir,
    const std::string& run_id,
    const std::string& snapshot_timestamp
);

} // namespace btrfsbackup
