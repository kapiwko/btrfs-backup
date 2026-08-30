// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/model/BackupRunPlan.hpp>
#include <backup/model/IncrementalParent.hpp>
#include <backup/model/PendingRecovery.hpp>
#include <backup/model/RetentionPlan.hpp>

#include <filesystem>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <core/Errors.hpp>
#include <core/Identifiers.hpp>
#include <config/model/Validation.hpp>

namespace fs = std::filesystem;

namespace {

const std::vector<btrfsbackup::backup::SnapshotInfo>& snapshots_for(
    const btrfsbackup::backup::SnapshotInventoryBySource& inventories,
    const btrfsbackup::SourceId& source_id
) {
    static const std::vector<btrfsbackup::backup::SnapshotInfo> empty;
    auto found = inventories.find(source_id);
    return found == inventories.end() ? empty : found->second;
}

std::optional<btrfsbackup::backup::PendingMarker> pending_marker_for(
    const btrfsbackup::backup::PendingMarkerBySource& markers,
    const btrfsbackup::SourceId& source_id
) {
    auto found = markers.find(source_id);
    return found == markers.end() ? std::nullopt : found->second;
}

std::optional<btrfsbackup::backup::SnapshotMetadata> pending_snapshot_for(
    const btrfsbackup::backup::PendingSnapshotBySource& snapshots,
    const btrfsbackup::SourceId& source_id
) {
    auto found = snapshots.find(source_id);
    return found == snapshots.end() ? std::nullopt : found->second;
}

bool name_exists(const std::vector<btrfsbackup::backup::SnapshotInfo>& snapshots, const std::string& name) {
    for (const btrfsbackup::backup::SnapshotInfo& snapshot : snapshots) {
        if (snapshot.name == name) {
            return true;
        }
    }
    return false;
}

std::string planned_snapshot_name(
    const btrfsbackup::SourceId& source_id,
    const std::string& timestamp,
    const std::vector<btrfsbackup::backup::SnapshotInfo>& local_snapshots
) {
    const std::string source_id_value{source_id.value()};
    const std::string base = source_id_value + "-" + timestamp;
    if (!name_exists(local_snapshots, base)) {
        return base;
    }

    for (int sequence = 1; sequence <= 99; ++sequence) {
        char suffix[4]{};
        std::snprintf(suffix, sizeof(suffix), "-%02d", sequence);
        const std::string candidate = base + suffix;
        if (!name_exists(local_snapshots, candidate)) {
            return candidate;
        }
    }

    throw btrfsbackup::ValidationError("could not allocate snapshot name for " + source_id_value + " at " + timestamp);
}

btrfsbackup::backup::SnapshotInfo projected_snapshot(
    btrfsbackup::backup::SnapshotSide side,
    const btrfsbackup::SourceId& source_id,
    const std::string& name,
    const std::string& timestamp,
    const fs::path& path
) {
    std::optional<btrfsbackup::backup::SnapshotName> parsed = btrfsbackup::backup::parse_snapshot_name(name, source_id);
    if (!parsed.has_value()) {
        throw btrfsbackup::ValidationError("planned snapshot name is invalid: " + name);
    }
    return btrfsbackup::backup::SnapshotInfo{
        .side = side,
        .source_id = source_id,
        .name = name,
        .timestamp = timestamp,
        .sequence = parsed->sequence,
        .path = path,
        .readonly = true,
        .uuid = {},
        .received_uuid = {},
    };
}

} // namespace

namespace btrfsbackup::backup {

BackupSourceRunPlan::BackupSourceRunPlan(SourceId source_id, std::vector<BackupRunAction> actions)
    : source_id(std::move(source_id)), actions_(std::move(actions)) {
    std::set<BackupRunActionKind> singleton_actions;
    for (const BackupRunAction& action : actions_) {
        if (backup_run_action_source_id(action) != this->source_id) {
            throw ValidationError("backup source plan contains an action for a different source");
        }
        const BackupRunActionKind kind = backup_run_action_kind(action);
        if (kind == BackupRunActionKind::BeforeSnapshotHook || kind == BackupRunActionKind::AfterSnapshotHook) {
            continue;
        }
        if (!singleton_actions.insert(kind).second) {
            throw ValidationError("backup source plan contains a duplicate action kind");
        }
    }
}

const std::vector<BackupRunAction>& BackupSourceRunPlan::actions() const noexcept {
    return actions_;
}

BackupRunPlan build_backup_run_plan(
    const btrfsbackup::config::Profile& profile,
    const SnapshotInventoryBySource& local_inventory,
    const SnapshotInventoryBySource& remote_inventory,
    const PendingMarkerBySource& pending_markers,
    const PendingSnapshotBySource& pending_snapshots,
    const fs::path& profile_state_dir,
    const RunId& run_id,
    const std::string& snapshot_timestamp
) {
    const SourceId timestamp_validation_source{std::string(profile.id.value())};
    const std::string run_id_value{run_id.value()};
    if (!parse_snapshot_name(std::string(profile.id.value()) + "-" + snapshot_timestamp, timestamp_validation_source).has_value()) {
        throw ValidationError("snapshot timestamp is invalid: " + snapshot_timestamp);
    }

    BackupRunPlan run_plan{
        .profile_id = profile.id,
        .run_id = run_id,
        .target_mount_point = profile.target.mount_point,
        .sources = {},
    };

    std::set<SourceId> seen_sources;
    for (const btrfsbackup::config::ProfileSource& source : profile.sources) {
        if (!source.enabled) {
            continue;
        }
        const SourceId& source_id = source.id;
        const std::string source_id_value{source_id.value()};
        if (!seen_sources.insert(source_id).second) {
            throw ValidationError("duplicate source id in backup run plan: " + source_id_value);
        }

        const fs::path remote_snapshot_dir = profile.paths.remote_root.value() / source.remote_subdir.value();
        const fs::path incoming_source_root = profile.paths.incoming_root.value() / source_id_value;
        const fs::path incoming_run_dir = incoming_source_root / run_id_value;

        if (!btrfsbackup::config::path_is_within(remote_snapshot_dir, profile.paths.remote_root.value())) {
            throw ValidationError("Remote source directory escapes REMOTE_ROOT: " + remote_snapshot_dir.string());
        }
        if (!btrfsbackup::config::path_is_within(incoming_source_root, profile.paths.incoming_root.value())) {
            throw ValidationError("Incoming source directory escapes INCOMING_ROOT: " + incoming_source_root.string());
        }
        if (btrfsbackup::config::path_is_within(source.local_snapshot_dir, profile.target.mount_point)) {
            throw ValidationError(
                "LOCAL_SNAPSHOT_DIR must not be inside the backup target: " + source.local_snapshot_dir.value().string()
            );
        }

        const std::vector<SnapshotInfo>& current_local_snapshots = snapshots_for(local_inventory, source_id);
        const std::vector<SnapshotInfo>& current_remote_snapshots = snapshots_for(remote_inventory, source_id);
        const std::string snapshot_name = planned_snapshot_name(source_id, snapshot_timestamp, current_local_snapshots);
        const fs::path local_snapshot_path = fs::path(source.local_snapshot_dir) / snapshot_name;
        const fs::path received_snapshot_path = incoming_run_dir / snapshot_name;
        const fs::path final_remote_snapshot_path = remote_snapshot_dir / snapshot_name;

        IncrementalParentSelection parent = select_incremental_parent(
            source_id,
            current_local_snapshots,
            current_remote_snapshots,
            local_snapshot_path,
            profile.settings.incremental_required
        );

        PendingRecoveryPlan recovery = plan_pending_recovery(
            source_id,
            profile_state_dir,
            source.local_snapshot_dir,
            remote_snapshot_dir,
            pending_marker_for(pending_markers, source_id),
            pending_snapshot_for(pending_snapshots, source_id),
            current_remote_snapshots,
            profile.settings.keep_failed_local_snapshot
        );

        std::vector<SnapshotInfo> projected_local = current_local_snapshots;
        if (const auto* effect = pending_recovery_effect<DeletePendingLocalSnapshot>(recovery)) {
            std::erase_if(projected_local, [&](const SnapshotInfo& snapshot) {
                return snapshot.path == effect->snapshot_path;
            });
        }
        projected_local.push_back(projected_snapshot(SnapshotSide::Local, source_id, snapshot_name, snapshot_timestamp, local_snapshot_path));
        std::vector<SnapshotInfo> projected_remote = current_remote_snapshots;
        if (const auto* effect = pending_recovery_effect<DeletePendingRemoteSnapshot>(recovery)) {
            std::erase_if(projected_remote, [&](const SnapshotInfo& snapshot) {
                return snapshot.path == effect->snapshot_path;
            });
        }
        projected_remote.push_back(projected_snapshot(SnapshotSide::Remote, source_id, snapshot_name, snapshot_timestamp, final_remote_snapshot_path));

        RetentionPlan local_retention = plan_count_retention(source_id, projected_local, source.local_retention.value());
        RetentionPlan remote_retention = plan_count_retention(source_id, projected_remote, source.remote_retention.value());

        const auto* incremental_parent = std::get_if<IncrementalTransfer>(&parent);
        const std::optional<fs::path> parent_path = incremental_parent != nullptr
            ? std::optional<fs::path>(incremental_parent->local_parent.path)
            : std::nullopt;
        std::vector<BackupRunAction> actions;
        if (recovery.required()) {
            actions.emplace_back(RecoverPendingAction{source_id, recovery});
        }
        actions.emplace_back(CleanupIncomingAction{source_id, incoming_source_root});
        for (const btrfsbackup::config::ProfileHookCommand& hook : profile.hooks.before_snapshot) {
            actions.emplace_back(RunHookAction{source_id, HookPhase::BeforeSnapshot, hook});
        }
        actions.emplace_back(CreateSnapshotAction{
            source_id,
            source.subvolume,
            source.local_snapshot_dir,
            local_snapshot_path,
            final_remote_snapshot_path,
            profile_state_dir,
            run_id,
        });
        for (const btrfsbackup::config::ProfileHookCommand& hook : profile.hooks.after_snapshot) {
            actions.emplace_back(RunHookAction{source_id, HookPhase::AfterSnapshot, hook});
        }
        actions.emplace_back(SendReceiveAction{
            source_id,
            local_snapshot_path,
            parent_path,
            remote_snapshot_dir,
            incoming_run_dir,
        });
        actions.emplace_back(VerifyReceivedAction{
            source_id,
            local_snapshot_path,
            received_snapshot_path,
        });
        actions.emplace_back(CommitReceivedAction{
            source_id,
            local_snapshot_path,
            received_snapshot_path,
            final_remote_snapshot_path,
        });
        actions.emplace_back(ApplyRemoteRetentionAction{
            source_id,
            remote_retention,
        });
        actions.emplace_back(ApplyLocalRetentionAction{
            source_id,
            local_retention,
        });
        actions.emplace_back(CleanupSourceAction{
            source_id,
            received_snapshot_path,
            incoming_run_dir,
            recovery.marker_path,
            profile_state_dir,
        });

        run_plan.sources.emplace_back(source.id, std::move(actions));
    }

    return run_plan;
}

} // namespace btrfsbackup::backup
