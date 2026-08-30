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
#include <config/domain/Validation.hpp>

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
    btrfsbackup::RuntimeTimePoint timestamp,
    const std::vector<btrfsbackup::backup::SnapshotInfo>& local_snapshots
) {
    const std::string source_id_value{source_id.value()};
    const std::string timestamp_text = btrfsbackup::format_utc_snapshot_timestamp(timestamp);
    const std::string base = source_id_value + "-" + timestamp_text;
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

    throw btrfsbackup::ValidationError("could not allocate snapshot name for " + source_id_value + " at " + timestamp_text);
}

btrfsbackup::backup::SnapshotInfo projected_snapshot(
    btrfsbackup::backup::SnapshotSide side,
    const btrfsbackup::SourceId& source_id,
    const std::string& name,
    btrfsbackup::RuntimeTimePoint timestamp,
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

namespace {

struct SourcePlanContext {
    const btrfsbackup::config::Profile& profile;
    const SnapshotInventoryBySource& local_inventory;
    const SnapshotInventoryBySource& remote_inventory;
    const PendingMarkerBySource& pending_markers;
    const PendingSnapshotBySource& pending_snapshots;
    const fs::path& profile_state_dir;
    const RunId& run_id;
    RuntimeTimePoint snapshot_timestamp;
};

struct PlannedSourcePaths {
    fs::path remote_snapshot_directory;
    fs::path incoming_source_root;
    fs::path incoming_run_directory;
    fs::path local_snapshot;
    fs::path received_snapshot;
    fs::path final_remote_snapshot;
    std::string snapshot_name;
};

PlannedSourcePaths plan_source_paths(
    const btrfsbackup::config::ProfileSource& source,
    const SourcePlanContext& context,
    const std::vector<SnapshotInfo>& local_snapshots
) {
    const std::string source_id{source.id.value()};
    const fs::path remote_directory = context.profile.paths.remote_root.value() / source.remote_subdir.value();
    const fs::path incoming_root = context.profile.paths.incoming_root.value() / source_id;
    const fs::path incoming_run = incoming_root / context.run_id.value();

    if (!btrfsbackup::config::path_is_within(remote_directory, context.profile.paths.remote_root.value())) {
        throw ValidationError("Remote source directory escapes REMOTE_ROOT: " + remote_directory.string());
    }
    if (!btrfsbackup::config::path_is_within(incoming_root, context.profile.paths.incoming_root.value())) {
        throw ValidationError("Incoming source directory escapes INCOMING_ROOT: " + incoming_root.string());
    }
    if (btrfsbackup::config::path_is_within(source.local_snapshot_dir, context.profile.target.mount_point)) {
        throw ValidationError(
            "LOCAL_SNAPSHOT_DIR must not be inside the backup target: " + source.local_snapshot_dir.value().string()
        );
    }

    const std::string snapshot_name = planned_snapshot_name(source.id, context.snapshot_timestamp, local_snapshots);
    return {
        .remote_snapshot_directory = remote_directory,
        .incoming_source_root = incoming_root,
        .incoming_run_directory = incoming_run,
        .local_snapshot = fs::path(source.local_snapshot_dir) / snapshot_name,
        .received_snapshot = incoming_run / snapshot_name,
        .final_remote_snapshot = remote_directory / snapshot_name,
        .snapshot_name = snapshot_name,
    };
}

std::vector<SnapshotInfo> projected_snapshots(
    const std::vector<SnapshotInfo>& current,
    const PendingRecoveryPlan& recovery,
    SnapshotSide side,
    const SourceId& source_id,
    const PlannedSourcePaths& paths,
    RuntimeTimePoint timestamp
) {
    std::vector<SnapshotInfo> projected = current;
    fs::path removed_path;
    if (side == SnapshotSide::Local) {
        if (const auto* deletion = pending_recovery_effect<DeletePendingLocalSnapshot>(recovery)) {
            removed_path = deletion->snapshot_path;
        }
    } else if (const auto* deletion = pending_recovery_effect<DeletePendingRemoteSnapshot>(recovery)) {
        removed_path = deletion->snapshot_path;
    }
    if (!removed_path.empty()) {
        std::erase_if(projected, [&](const SnapshotInfo& snapshot) {
            return snapshot.path == removed_path;
        });
    }
    const fs::path projected_path = side == SnapshotSide::Local
        ? paths.local_snapshot
        : paths.final_remote_snapshot;
    projected.push_back(projected_snapshot(side, source_id, paths.snapshot_name, timestamp, projected_path));
    return projected;
}

std::vector<BackupRunAction> ordered_source_actions(
    const btrfsbackup::config::ProfileSource& source,
    const SourcePlanContext& context,
    const PlannedSourcePaths& paths,
    const PendingRecoveryPlan& recovery,
    const IncrementalParentSelection& parent,
    RetentionPlan local_retention,
    RetentionPlan remote_retention
) {
    const SourceId& source_id = source.id;
    const auto* incremental_parent = std::get_if<IncrementalTransfer>(&parent);
    const std::optional<fs::path> parent_path = incremental_parent == nullptr
        ? std::nullopt
        : std::optional<fs::path>{incremental_parent->local_parent.path};

    std::vector<BackupRunAction> actions;
    if (recovery.required()) {
        actions.emplace_back(RecoverPendingAction{source_id, recovery});
    }
    actions.emplace_back(CleanupIncomingAction{source_id, paths.incoming_source_root});
    for (const btrfsbackup::config::ProfileHookCommand& hook : context.profile.hooks.before_snapshot) {
        actions.emplace_back(RunHookAction{source_id, HookPhase::BeforeSnapshot, hook});
    }
    actions.emplace_back(CreateSnapshotAction{
        source_id,
        source.subvolume,
        source.local_snapshot_dir,
        paths.local_snapshot,
        paths.final_remote_snapshot,
        context.profile_state_dir,
        context.run_id,
    });
    for (const btrfsbackup::config::ProfileHookCommand& hook : context.profile.hooks.after_snapshot) {
        actions.emplace_back(RunHookAction{source_id, HookPhase::AfterSnapshot, hook});
    }
    actions.emplace_back(SendReceiveAction{
        source_id,
        paths.local_snapshot,
        parent_path,
        paths.remote_snapshot_directory,
        paths.incoming_run_directory,
    });
    actions.emplace_back(VerifyReceivedAction{source_id, paths.local_snapshot, paths.received_snapshot});
    actions.emplace_back(CommitReceivedAction{
        source_id,
        paths.local_snapshot,
        paths.received_snapshot,
        paths.final_remote_snapshot,
    });
    actions.emplace_back(ApplyRemoteRetentionAction{source_id, std::move(remote_retention)});
    actions.emplace_back(ApplyLocalRetentionAction{source_id, std::move(local_retention)});
    actions.emplace_back(CleanupSourceAction{
        source_id,
        paths.received_snapshot,
        paths.incoming_run_directory,
        recovery.marker_path,
        context.profile_state_dir,
    });
    return actions;
}

BackupSourceRunPlan build_source_run_plan(
    const btrfsbackup::config::ProfileSource& source,
    const SourcePlanContext& context
) {
    const std::vector<SnapshotInfo>& local = snapshots_for(context.local_inventory, source.id);
    const std::vector<SnapshotInfo>& remote = snapshots_for(context.remote_inventory, source.id);
    const PlannedSourcePaths paths = plan_source_paths(source, context, local);

    const IncrementalParentSelection parent = select_incremental_parent(
        source.id,
        local,
        remote,
        paths.local_snapshot,
        context.profile.settings.incremental_required
    );
    const PendingRecoveryPlan recovery = plan_pending_recovery(
        source.id,
        context.profile_state_dir,
        source.local_snapshot_dir,
        paths.remote_snapshot_directory,
        pending_marker_for(context.pending_markers, source.id),
        pending_snapshot_for(context.pending_snapshots, source.id),
        remote,
        context.profile.settings.keep_failed_local_snapshot
    );
    RetentionPlan local_retention = plan_count_retention(
        source.id,
        projected_snapshots(local, recovery, SnapshotSide::Local, source.id, paths, context.snapshot_timestamp),
        source.local_retention.value()
    );
    RetentionPlan remote_retention = plan_count_retention(
        source.id,
        projected_snapshots(remote, recovery, SnapshotSide::Remote, source.id, paths, context.snapshot_timestamp),
        source.remote_retention.value()
    );
    return BackupSourceRunPlan{
        source.id,
        ordered_source_actions(
            source,
            context,
            paths,
            recovery,
            parent,
            std::move(local_retention),
            std::move(remote_retention)
        ),
    };
}

} // namespace

BackupRunPlan build_backup_run_plan(
    const btrfsbackup::config::Profile& profile,
    const SnapshotInventoryBySource& local_inventory,
    const SnapshotInventoryBySource& remote_inventory,
    const PendingMarkerBySource& pending_markers,
    const PendingSnapshotBySource& pending_snapshots,
    const fs::path& profile_state_dir,
    const RunId& run_id,
    RuntimeTimePoint snapshot_timestamp
) {
    BackupRunPlan run_plan{
        .profile_id = profile.id,
        .run_id = run_id,
        .target_mount_point = profile.target.mount_point,
        .sources = {},
    };
    const SourcePlanContext context{
        profile,
        local_inventory,
        remote_inventory,
        pending_markers,
        pending_snapshots,
        profile_state_dir,
        run_id,
        snapshot_timestamp,
    };

    std::set<SourceId> seen_sources;
    for (const btrfsbackup::config::ProfileSource& source : profile.sources) {
        if (!source.enabled) {
            continue;
        }
        if (!seen_sources.insert(source.id).second) {
            throw ValidationError("duplicate source id in backup run plan: " + std::string(source.id.value()));
        }
        run_plan.sources.push_back(build_source_run_plan(source, context));
    }

    return run_plan;
}

} // namespace btrfsbackup::backup
