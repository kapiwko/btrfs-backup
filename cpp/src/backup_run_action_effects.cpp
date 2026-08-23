#include <btrfsbackup/backup_run_action_effects.hpp>

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/run_state.hpp>
#include <btrfsbackup/snapshot_transfer.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

namespace {

std::string current_utc_iso_timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&time, &tm);

    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

fs::path profile_state_dir_for_source(const BackupSourceRunPlan& source_plan) {
    return source_plan.recovery.marker_path.parent_path();
}

void cleanup_path(IBtrfsOperations& btrfs, IFileSystemEffects& fs_effects, const fs::path& path) {
    if (!fs_effects.exists(path)) {
        return;
    }
    if (btrfs.is_subvolume(path)) {
        btrfs.delete_subvolume(path);
        return;
    }
    fs_effects.remove_tree(path);
}

void cleanup_directory_contents(IBtrfsOperations& btrfs, IFileSystemEffects& fs_effects, const fs::path& directory) {
    if (!fs_effects.is_directory(directory)) {
        return;
    }
    for (const fs::path& entry : fs_effects.list_directory(directory)) {
        cleanup_path(btrfs, fs_effects, entry);
    }
}

void cleanup_incoming_run_dir(IBtrfsOperations& btrfs, IFileSystemEffects& fs_effects, const fs::path& run_dir) {
    if (!fs_effects.is_directory(run_dir)) {
        return;
    }
    cleanup_directory_contents(btrfs, fs_effects, run_dir);
    fs_effects.remove_directory(run_dir);
}

SnapshotMetadata require_snapshot_metadata(IBtrfsOperations& btrfs, const fs::path& path, const std::string& message) {
    std::optional<SnapshotMetadata> metadata = btrfs.read_snapshot_metadata(path);
    if (!metadata.has_value()) {
        throw ValidationError(message + ": " + path.string());
    }
    return *metadata;
}

void apply_retention(IBtrfsOperations& btrfs, const RetentionPlan& plan) {
    for (const SnapshotInfo& snapshot : plan.delete_snapshots) {
        btrfs.delete_subvolume(snapshot.path);
    }
}

void recover_pending(
    IBtrfsOperations& btrfs,
    const BackupSourceRunPlan& source_plan
) {
    if (source_plan.recovery.delete_local_snapshot) {
        btrfs.delete_subvolume(source_plan.recovery.local_snapshot_path);
    }
    if (source_plan.recovery.clear_marker) {
        clear_pending_marker(source_plan.recovery.marker_path, profile_state_dir_for_source(source_plan));
    }
}

void create_local_snapshot(
    IBtrfsOperations& btrfs,
    IFileSystemEffects& fs_effects,
    const BackupSourceRunPlan& source_plan,
    const BackupRunPlan& run_plan
) {
    fs_effects.create_directories(source_plan.local_snapshot_dir);
    write_pending_marker(
        profile_state_dir_for_source(source_plan),
        PendingMarker{
            .source_name = source_plan.source_id,
            .local_snapshot_path = source_plan.local_snapshot_path.string(),
            .run_id = run_plan.run_id,
            .timestamp = current_utc_iso_timestamp(),
        }
    );

    btrfs.create_readonly_snapshot(source_plan.source_subvolume, source_plan.local_snapshot_path);
    SnapshotMetadata metadata = require_snapshot_metadata(
        btrfs,
        source_plan.local_snapshot_path,
        "New local snapshot metadata is missing"
    );
    if (!metadata.is_subvolume || !metadata.readonly) {
        throw ValidationError("New local snapshot is not readonly: " + source_plan.local_snapshot_path.string());
    }
}

void verify_received(IBtrfsOperations& btrfs, const BackupSourceRunPlan& source_plan) {
    SnapshotMetadata local = require_snapshot_metadata(
        btrfs,
        source_plan.local_snapshot_path,
        "Local snapshot metadata is missing"
    );
    SnapshotMetadata received = require_snapshot_metadata(
        btrfs,
        source_plan.received_snapshot_path,
        "Received snapshot metadata is missing"
    );
    verify_received_snapshot(source_plan.source_id, local, received);
}

void commit_received(IBtrfsOperations& btrfs, IFileSystemEffects& fs_effects, const BackupSourceRunPlan& source_plan) {
    SnapshotMetadata local = require_snapshot_metadata(
        btrfs,
        source_plan.local_snapshot_path,
        "Local snapshot metadata is missing"
    );
    commit_received_snapshot(
        btrfs,
        fs_effects,
        source_plan.received_snapshot_path,
        source_plan.final_remote_snapshot_path,
        local.uuid
    );
}

void cleanup_source(IBtrfsOperations& btrfs, IFileSystemEffects& fs_effects, const BackupSourceRunPlan& source_plan) {
    cleanup_path(btrfs, fs_effects, source_plan.received_snapshot_path);
    cleanup_incoming_run_dir(btrfs, fs_effects, source_plan.incoming_run_dir);
    clear_pending_marker(source_plan.recovery.marker_path, profile_state_dir_for_source(source_plan));
}

void run_hook(ICommandRunner* hooks, const BackupRunAction& action) {
    if (hooks == nullptr) {
        throw ValidationError("hook execution is not configured");
    }
    if (action.hook.program.empty()) {
        throw ValidationError("hook program is required");
    }

    std::vector<std::string> argv;
    argv.reserve(action.hook.arguments.size() + 1);
    argv.push_back(action.hook.program);
    argv.insert(argv.end(), action.hook.arguments.begin(), action.hook.arguments.end());

    CommandResult result = hooks->run(argv);
    if (result.exit_code != 0) {
        throw ValidationError("hook failed: " + action.hook.program);
    }
}

} // namespace

BackupRunActionEffects::BackupRunActionEffects(IBtrfsOperations& btrfs, IFileSystemEffects& fs_effects)
    : btrfs_(btrfs),
      fs_effects_(fs_effects) {
}

BackupRunActionEffects::BackupRunActionEffects(IBtrfsOperations& btrfs, IFileSystemEffects& fs_effects, ICommandRunner& hooks)
    : btrfs_(btrfs),
      fs_effects_(fs_effects),
      hooks_(&hooks) {
}

void BackupRunActionEffects::execute_action(
    const BackupRunAction& action,
    const BackupSourceRunPlan& source_plan,
    const BackupRunPlan& run_plan
) {
    switch (action.kind) {
        case BackupRunActionKind::RecoverPending:
            recover_pending(btrfs_, source_plan);
            return;
        case BackupRunActionKind::CleanupIncoming:
            cleanup_directory_contents(btrfs_, fs_effects_, source_plan.incoming_source_root);
            return;
        case BackupRunActionKind::BeforeSnapshotHook:
        case BackupRunActionKind::AfterSnapshotHook:
            run_hook(hooks_, action);
            return;
        case BackupRunActionKind::CreateSnapshot:
            create_local_snapshot(btrfs_, fs_effects_, source_plan, run_plan);
            return;
        case BackupRunActionKind::VerifyReceived:
            verify_received(btrfs_, source_plan);
            return;
        case BackupRunActionKind::CommitReceived:
            commit_received(btrfs_, fs_effects_, source_plan);
            return;
        case BackupRunActionKind::ApplyRemoteRetention:
            apply_retention(btrfs_, source_plan.remote_retention);
            return;
        case BackupRunActionKind::ApplyLocalRetention:
            apply_retention(btrfs_, source_plan.local_retention);
            return;
        case BackupRunActionKind::CleanupSource:
            cleanup_source(btrfs_, fs_effects_, source_plan);
            return;
        case BackupRunActionKind::SelectParent:
        case BackupRunActionKind::SendReceive:
            return;
    }
}

} // namespace btrfsbackup
