#include <backup/backup_run_plan.hpp>

#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <config/errors.hpp>
#include <config/identifiers.hpp>
#include <config/validation.hpp>

namespace fs = std::filesystem;

namespace {

const std::vector<btrfsbackup::SnapshotInfo>& snapshots_for(
    const btrfsbackup::SnapshotInventoryBySource& inventories,
    const std::string& source_id
) {
    static const std::vector<btrfsbackup::SnapshotInfo> empty;
    auto found = inventories.find(source_id);
    return found == inventories.end() ? empty : found->second;
}

std::optional<btrfsbackup::PendingMarker> pending_marker_for(
    const btrfsbackup::PendingMarkerBySource& markers,
    const std::string& source_id
) {
    auto found = markers.find(source_id);
    return found == markers.end() ? std::nullopt : found->second;
}

std::optional<btrfsbackup::SnapshotMetadata> pending_snapshot_for(
    const btrfsbackup::PendingSnapshotBySource& snapshots,
    const std::string& source_id
) {
    auto found = snapshots.find(source_id);
    return found == snapshots.end() ? std::nullopt : found->second;
}

bool name_exists(const std::vector<btrfsbackup::SnapshotInfo>& snapshots, const std::string& name) {
    for (const btrfsbackup::SnapshotInfo& snapshot : snapshots) {
        if (snapshot.name == name) {
            return true;
        }
    }
    return false;
}

std::string planned_snapshot_name(
    const std::string& source_id,
    const std::string& timestamp,
    const std::vector<btrfsbackup::SnapshotInfo>& local_snapshots
) {
    const std::string base = source_id + "-" + timestamp;
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

    throw btrfsbackup::ValidationError("could not allocate snapshot name for " + source_id + " at " + timestamp);
}

btrfsbackup::SnapshotInfo projected_snapshot(
    btrfsbackup::SnapshotSide side,
    const std::string& source_id,
    const std::string& name,
    const std::string& timestamp,
    const fs::path& path
) {
    std::optional<btrfsbackup::SnapshotName> parsed = btrfsbackup::parse_snapshot_name(name, source_id);
    if (!parsed.has_value()) {
        throw btrfsbackup::ValidationError("planned snapshot name is invalid: " + name);
    }
    return btrfsbackup::SnapshotInfo{
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

void require_source_mount_constraints(
    const btrfsbackup::ProfileSource& source,
    const btrfsbackup::Profile& profile,
    const std::vector<btrfsbackup::MountEntry>& mounts
) {
    if (!btrfsbackup::paths_are_same_filesystem(mounts, source.subvolume, source.local_snapshot_dir)) {
        throw btrfsbackup::ValidationError("LOCAL_SNAPSHOT_DIR must be on the same Btrfs filesystem as " + source.subvolume);
    }
    if (btrfsbackup::paths_are_same_filesystem(mounts, source.subvolume, profile.target.mount_point)) {
        throw btrfsbackup::ValidationError("SOURCE_SUBVOLUME must not be on the backup target filesystem: " + source.subvolume);
    }
    if (btrfsbackup::path_is_within(source.local_snapshot_dir, profile.target.mount_point)) {
        throw btrfsbackup::ValidationError("LOCAL_SNAPSHOT_DIR must not be inside the backup target: " + source.local_snapshot_dir);
    }
}

void add_action(
    std::vector<btrfsbackup::BackupRunAction>& actions,
    btrfsbackup::BackupRunActionKind kind,
    const std::string& source_id,
    const fs::path& primary_path = {},
    const fs::path& secondary_path = {},
    const btrfsbackup::ProfileHookCommand& hook = {}
) {
    actions.push_back({
        .kind = kind,
        .source_id = source_id,
        .primary_path = primary_path,
        .secondary_path = secondary_path,
        .hook = hook,
    });
}

} // namespace

namespace btrfsbackup {

BackupRunPlan build_backup_run_plan(
    const Profile& profile,
    const std::vector<MountEntry>& mounts,
    const SnapshotInventoryBySource& local_inventory,
    const SnapshotInventoryBySource& remote_inventory,
    const PendingMarkerBySource& pending_markers,
    const PendingSnapshotBySource& pending_snapshots,
    const fs::path& profile_state_dir,
    const std::string& run_id,
    const std::string& snapshot_timestamp
) {
    validate_profile_id(profile.id);
    validate_run_id(run_id);
    if (!parse_snapshot_name(profile.id + "-" + snapshot_timestamp, profile.id).has_value()) {
        throw ValidationError("snapshot timestamp is invalid: " + snapshot_timestamp);
    }

    BackupRunPlan run_plan{
        .profile_id = profile.id,
        .run_id = run_id,
        .target_mount_point = profile.target.mount_point,
        .sources = {},
    };

    std::set<std::string> seen_sources;
    for (const ProfileSource& source : profile.sources) {
        if (!source.enabled) {
            continue;
        }
        validate_identifier(source.id, "sourceId");
        if (!seen_sources.insert(source.id).second) {
            throw ValidationError("duplicate source id in backup run plan: " + source.id);
        }

        const fs::path remote_snapshot_dir = fs::path(profile.paths.remote_root) / source.remote_subdir;
        const fs::path incoming_source_root = fs::path(profile.paths.incoming_root) / source.id;
        const fs::path incoming_run_dir = incoming_source_root / run_id;

        if (!path_is_within(remote_snapshot_dir, profile.paths.remote_root)) {
            throw ValidationError("Remote source directory escapes REMOTE_ROOT: " + remote_snapshot_dir.string());
        }
        if (!path_is_within(incoming_source_root, profile.paths.incoming_root)) {
            throw ValidationError("Incoming source directory escapes INCOMING_ROOT: " + incoming_source_root.string());
        }
        require_source_mount_constraints(source, profile, mounts);

        const std::vector<SnapshotInfo>& current_local_snapshots = snapshots_for(local_inventory, source.id);
        const std::vector<SnapshotInfo>& current_remote_snapshots = snapshots_for(remote_inventory, source.id);
        const std::string snapshot_name = planned_snapshot_name(source.id, snapshot_timestamp, current_local_snapshots);
        const fs::path local_snapshot_path = fs::path(source.local_snapshot_dir) / snapshot_name;
        const fs::path received_snapshot_path = incoming_run_dir / snapshot_name;
        const fs::path final_remote_snapshot_path = remote_snapshot_dir / snapshot_name;

        IncrementalParentSelection parent = select_incremental_parent(
            source.id,
            current_local_snapshots,
            current_remote_snapshots,
            local_snapshot_path,
            profile.settings.incremental_required
        );

        PendingRecoveryPlan recovery = plan_pending_recovery(
            source.id,
            profile_state_dir,
            source.local_snapshot_dir,
            remote_snapshot_dir,
            pending_marker_for(pending_markers, source.id),
            pending_snapshot_for(pending_snapshots, source.id),
            current_remote_snapshots,
            profile.settings.keep_failed_local_snapshot
        );

        std::vector<SnapshotInfo> projected_local = current_local_snapshots;
        if (recovery.delete_local_snapshot) {
            std::erase_if(projected_local, [&](const SnapshotInfo& snapshot) {
                return snapshot.path == recovery.local_snapshot_path;
            });
        }
        projected_local.push_back(projected_snapshot(SnapshotSide::Local, source.id, snapshot_name, snapshot_timestamp, local_snapshot_path));
        std::vector<SnapshotInfo> projected_remote = current_remote_snapshots;
        if (recovery.delete_remote_snapshot) {
            std::erase_if(projected_remote, [&](const SnapshotInfo& snapshot) {
                return snapshot.path == recovery.remote_snapshot_path;
            });
        }
        projected_remote.push_back(projected_snapshot(SnapshotSide::Remote, source.id, snapshot_name, snapshot_timestamp, final_remote_snapshot_path));

        RetentionPlan local_retention = plan_count_retention(source.id, projected_local, source.local_retention);
        RetentionPlan remote_retention = plan_count_retention(source.id, projected_remote, source.remote_retention);

        BackupSourceRunPlan source_plan{
            .source_id = source.id,
            .source_subvolume = source.subvolume,
            .local_snapshot_dir = source.local_snapshot_dir,
            .remote_snapshot_dir = remote_snapshot_dir,
            .incoming_source_root = incoming_source_root,
            .incoming_run_dir = incoming_run_dir,
            .local_snapshot_path = local_snapshot_path,
            .received_snapshot_path = received_snapshot_path,
            .final_remote_snapshot_path = final_remote_snapshot_path,
            .parent = parent,
            .recovery = recovery,
            .local_retention = local_retention,
            .remote_retention = remote_retention,
            .actions = {},
        };

        if (recovery.action != PendingRecoveryAction::NoMarker) {
            add_action(source_plan.actions, BackupRunActionKind::RecoverPending, source.id, recovery.local_snapshot_path);
        }
        add_action(source_plan.actions, BackupRunActionKind::CleanupIncoming, source.id, incoming_source_root);
        for (const ProfileHookCommand& hook : profile.hooks.before_snapshot) {
            add_action(source_plan.actions, BackupRunActionKind::BeforeSnapshotHook, source.id, {}, {}, hook);
        }
        add_action(source_plan.actions, BackupRunActionKind::CreateSnapshot, source.id, local_snapshot_path, source.subvolume);
        for (const ProfileHookCommand& hook : profile.hooks.after_snapshot) {
            add_action(source_plan.actions, BackupRunActionKind::AfterSnapshotHook, source.id, {}, {}, hook);
        }
        add_action(source_plan.actions, BackupRunActionKind::SelectParent, source.id, parent.local_parent.has_value() ? parent.local_parent->path : fs::path{});
        add_action(source_plan.actions, BackupRunActionKind::SendReceive, source.id, local_snapshot_path, incoming_run_dir);
        add_action(source_plan.actions, BackupRunActionKind::VerifyReceived, source.id, received_snapshot_path, local_snapshot_path);
        add_action(source_plan.actions, BackupRunActionKind::CommitReceived, source.id, received_snapshot_path, final_remote_snapshot_path);
        add_action(source_plan.actions, BackupRunActionKind::ApplyRemoteRetention, source.id, remote_snapshot_dir);
        add_action(source_plan.actions, BackupRunActionKind::ApplyLocalRetention, source.id, source.local_snapshot_dir);
        add_action(source_plan.actions, BackupRunActionKind::CleanupSource, source.id);

        run_plan.sources.push_back(source_plan);
    }

    return run_plan;
}

} // namespace btrfsbackup
