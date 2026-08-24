#include <backup/backup_run_action_effects.hpp>

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <config/errors.hpp>
#include <state/run_state.hpp>
#include <backup/snapshot_transfer.hpp>

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

void cleanup_directory_contents(
    IBtrfsOperations& btrfs,
    IFileSystem& fs_effects,
    const fs::path& directory,
    const SafeDirectoryRoot* safe_root
);

void cleanup_path(
    IBtrfsOperations& btrfs,
    IFileSystem& fs_effects,
    const fs::path& path,
    const SafeDirectoryRoot* safe_root
) {
    if (safe_root != nullptr) {
        safe_root->remove_tree(path);
        return;
    }
    if (!fs_effects.exists(path)) {
        return;
    }
    if (btrfs.is_subvolume(path)) {
        btrfs.delete_subvolume(path);
        return;
    }
    if (fs_effects.is_directory(path)) {
        cleanup_directory_contents(btrfs, fs_effects, path, nullptr);
        fs_effects.remove_directory(path);
        return;
    }
    fs_effects.remove_tree(path);
}

void cleanup_directory_contents(
    IBtrfsOperations& btrfs,
    IFileSystem& fs_effects,
    const fs::path& directory,
    const SafeDirectoryRoot* safe_root
) {
    if (safe_root != nullptr) {
        safe_root->remove_contents(directory);
        return;
    }
    if (!fs_effects.is_directory(directory)) {
        return;
    }
    for (const fs::path& entry : fs_effects.list_directory(directory)) {
        cleanup_path(btrfs, fs_effects, entry, nullptr);
    }
}

void cleanup_incoming_run_dir(
    IBtrfsOperations& btrfs,
    IFileSystem& fs_effects,
    const fs::path& run_dir,
    const SafeDirectoryRoot* safe_root
) {
    if (safe_root != nullptr) {
        safe_root->remove_tree(run_dir);
        return;
    }
    if (!fs_effects.is_directory(run_dir)) {
        return;
    }
    cleanup_directory_contents(btrfs, fs_effects, run_dir, nullptr);
    fs_effects.remove_directory(run_dir);
}

SnapshotMetadata require_snapshot_metadata(
    IBtrfsOperations& btrfs,
    const fs::path& path,
    const std::string& message,
    const SafeDirectoryRoot* safe_root = nullptr
) {
    std::optional<SnapshotMetadata> metadata = safe_root == nullptr
        ? btrfs.read_snapshot_metadata(path)
        : btrfs.read_snapshot_metadata_beneath(*safe_root, path);
    if (!metadata.has_value()) {
        throw ValidationError(message + ": " + path.string());
    }
    return *metadata;
}

void apply_retention(IBtrfsOperations& btrfs, const RetentionPlan& plan, const SafeDirectoryRoot* safe_root) {
    for (const SnapshotInfo& snapshot : plan.delete_snapshots) {
        if (safe_root == nullptr) {
            btrfs.delete_subvolume(snapshot.path);
        } else {
            btrfs.delete_subvolume_beneath(*safe_root, snapshot.path);
        }
    }
}

void recover_pending(
    IBtrfsOperations& btrfs,
    const BackupSourceRunPlan& source_plan,
    const SafeDirectoryRoot* local_root,
    const SafeDirectoryRoot* target_root
) {
    if (source_plan.recovery.delete_remote_snapshot) {
        if (target_root == nullptr) {
            btrfs.delete_subvolume(source_plan.recovery.remote_snapshot_path);
        } else {
            btrfs.delete_subvolume_beneath(*target_root, source_plan.recovery.remote_snapshot_path);
        }
    }
    if (source_plan.recovery.delete_local_snapshot) {
        if (local_root == nullptr) {
            btrfs.delete_subvolume(source_plan.recovery.local_snapshot_path);
        } else {
            btrfs.delete_subvolume_beneath(*local_root, source_plan.recovery.local_snapshot_path);
        }
    }
    if (source_plan.recovery.clear_marker) {
        clear_pending_marker(source_plan.recovery.marker_path, profile_state_dir_for_source(source_plan));
    }
}

void create_local_snapshot(
    IBtrfsOperations& btrfs,
    IFileSystem& fs_effects,
    const BackupSourceRunPlan& source_plan,
    const BackupRunPlan& run_plan,
    const SafeDirectoryRoot* local_root
) {
    if (local_root == nullptr) {
        fs_effects.create_directories(source_plan.local_snapshot_dir);
    } else {
        local_root->ensure_directory(source_plan.local_snapshot_dir);
    }
    write_pending_marker(
        profile_state_dir_for_source(source_plan),
        PendingMarker{
            .source_name = source_plan.source_id.value,
            .local_snapshot_path = source_plan.local_snapshot_path.string(),
            .final_snapshot_path = source_plan.final_remote_snapshot_path.string(),
            .run_id = run_plan.run_id.value,
            .timestamp = current_utc_iso_timestamp(),
        }
    );

    if (local_root == nullptr) {
        btrfs.create_readonly_snapshot(source_plan.source_subvolume, source_plan.local_snapshot_path);
    } else {
        btrfs.create_readonly_snapshot_beneath(
            *local_root,
            source_plan.source_subvolume,
            *local_root,
            source_plan.local_snapshot_path
        );
    }
    SnapshotMetadata metadata = require_snapshot_metadata(
        btrfs,
        source_plan.local_snapshot_path,
        "New local snapshot metadata is missing",
        local_root
    );
    if (!metadata.is_subvolume || !metadata.readonly) {
        throw ValidationError("New local snapshot is not readonly: " + source_plan.local_snapshot_path.string());
    }
}

void verify_received(
    IBtrfsOperations& btrfs,
    const BackupSourceRunPlan& source_plan,
    const SafeDirectoryRoot* local_root,
    const SafeDirectoryRoot* target_root
) {
    SnapshotMetadata local = require_snapshot_metadata(
        btrfs,
        source_plan.local_snapshot_path,
        "Local snapshot metadata is missing",
        local_root
    );
    SnapshotMetadata received = require_snapshot_metadata(
        btrfs,
        source_plan.received_snapshot_path,
        "Received snapshot metadata is missing",
        target_root
    );
    verify_received_snapshot(source_plan.source_id.value, local, received);
}

void commit_received(
    IBtrfsOperations& btrfs,
    IFileSystem& fs_effects,
    const BackupSourceRunPlan& source_plan,
    const SafeDirectoryRoot* local_root,
    const SafeDirectoryRoot* target_root
) {
    SnapshotMetadata local = require_snapshot_metadata(
        btrfs,
        source_plan.local_snapshot_path,
        "Local snapshot metadata is missing",
        local_root
    );
    if (target_root == nullptr) {
        commit_received_snapshot(
            btrfs,
            fs_effects,
            source_plan.received_snapshot_path,
            source_plan.final_remote_snapshot_path,
            local.uuid
        );
    } else {
        commit_received_snapshot_beneath(
            btrfs,
            *target_root,
            source_plan.received_snapshot_path,
            source_plan.final_remote_snapshot_path,
            local.uuid
        );
    }
}

void prepare_send_receive(
    IFileSystem& fs_effects,
    const BackupSourceRunPlan& source_plan,
    const SafeDirectoryRoot* target_root
) {
    if (target_root == nullptr) {
        fs_effects.create_directories(source_plan.remote_snapshot_dir);
        fs_effects.create_directories(source_plan.incoming_run_dir);
    } else {
        target_root->ensure_directory(source_plan.remote_snapshot_dir);
        target_root->ensure_directory(source_plan.incoming_run_dir);
    }
}

void cleanup_source(
    IBtrfsOperations& btrfs,
    IFileSystem& fs_effects,
    const BackupSourceRunPlan& source_plan,
    const SafeDirectoryRoot* target_root
) {
    cleanup_path(btrfs, fs_effects, source_plan.received_snapshot_path, target_root);
    cleanup_incoming_run_dir(btrfs, fs_effects, source_plan.incoming_run_dir, target_root);
    clear_pending_marker(source_plan.recovery.marker_path, profile_state_dir_for_source(source_plan));
}

std::string hook_error_code(const BackupRunAction& action, const std::string& suffix) {
    const std::string phase = action.kind == BackupRunActionKind::BeforeSnapshotHook
        ? "before_snapshot"
        : "after_snapshot";
    return "hook." + phase + "_" + suffix;
}

void run_hook(
    ICommandRunner* hooks,
    const BackupRunAction& action,
    CancellationToken& cancellation,
    const SafeDirectoryRoot* hook_root,
    const TrustedExecutablePolicy& hook_policy
) {
    if (hooks == nullptr) {
        throw ValidationError("hook execution is not configured");
    }
    if (action.hook.program.empty()) {
        throw ValidationError("hook program is required");
    }
    if (action.hook.timeout_seconds < 1 || action.hook.timeout_seconds > 86400) {
        throw CodedValidationError(
            hook_error_code(action, "failed"),
            "hook timeout is outside the supported range: " + action.hook.program
        );
    }

    CommandResult result;
    try {
        std::optional<SafeDirectoryHandle> executable;
        std::vector<int> inherited_fds;
        std::string executable_path = action.hook.program;
        if (hook_root != nullptr) {
            executable.emplace(open_trusted_executable(*hook_root, action.hook.program, hook_policy));
            executable_path = executable->proc_path().string();
            inherited_fds.push_back(executable->fd());
        }
        std::vector<std::string> argv;
        argv.reserve(action.hook.arguments.size() + 1);
        argv.push_back(executable_path);
        argv.insert(argv.end(), action.hook.arguments.begin(), action.hook.arguments.end());
        result = hooks->run_controlled(argv, {
            .cancellation_fd = cancellation.cancellation_fd(),
            .timeout = std::chrono::seconds(action.hook.timeout_seconds),
            .inherited_fds = inherited_fds,
        });
    } catch (const std::exception& error) {
        throw CodedValidationError(
            hook_error_code(action, "failed"),
            "hook execution failed: " + action.hook.program + ": " + error.what()
        );
    }
    if (result.cancelled) {
        throw OperationCancelledError("hook cancelled: " + action.hook.program);
    }
    if (result.timed_out) {
        throw CodedValidationError(
            hook_error_code(action, "timeout"),
            "hook timed out after " + std::to_string(action.hook.timeout_seconds)
                + " seconds: " + action.hook.program
        );
    }
    if (result.exit_code != 0) {
        std::string message = "hook failed with exit code " + std::to_string(result.exit_code)
            + ": " + action.hook.program;
        throw CodedValidationError(hook_error_code(action, "failed"), message);
    }
}

} // namespace

BackupRunActionEffects::BackupRunActionEffects(IBtrfsOperations& btrfs, IFileSystem& fs_effects)
    : btrfs_(btrfs),
      fs_effects_(fs_effects) {
}

BackupRunActionEffects::BackupRunActionEffects(IBtrfsOperations& btrfs, IFileSystem& fs_effects, ICommandRunner& hooks)
    : btrfs_(btrfs),
      fs_effects_(fs_effects),
      hooks_(&hooks) {
}

BackupRunActionEffects::BackupRunActionEffects(
    IBtrfsOperations& btrfs,
    IFileSystem& fs_effects,
    ICommandRunner& hooks,
    const fs::path& target_mount_point
)
    : BackupRunActionEffects(
          btrfs,
          fs_effects,
          hooks,
          target_mount_point,
          trusted_hook_directory,
          {}
      ) {
}

BackupRunActionEffects::BackupRunActionEffects(
    IBtrfsOperations& btrfs,
    IFileSystem& fs_effects,
    ICommandRunner& hooks,
    const fs::path& target_mount_point,
    const fs::path& hook_root,
    const TrustedExecutablePolicy& hook_policy
)
    : btrfs_(btrfs),
      fs_effects_(fs_effects),
      hooks_(&hooks),
      local_root_(std::make_unique<SafeDirectoryRoot>("/")),
      target_root_(std::make_unique<SafeDirectoryRoot>(target_mount_point)),
      hook_root_path_(hook_root),
      hook_policy_(hook_policy) {
}

void BackupRunActionEffects::execute_action(
    const BackupRunAction& action,
    const BackupSourceRunPlan& source_plan,
    const BackupRunPlan& run_plan,
    CancellationToken& cancellation
) {
    switch (action.kind) {
        case BackupRunActionKind::RecoverPending:
            recover_pending(btrfs_, source_plan, local_root_.get(), target_root_.get());
            return;
        case BackupRunActionKind::CleanupIncoming:
            cleanup_directory_contents(btrfs_, fs_effects_, source_plan.incoming_source_root, target_root_.get());
            return;
        case BackupRunActionKind::BeforeSnapshotHook:
        case BackupRunActionKind::AfterSnapshotHook: {
            std::optional<SafeDirectoryRoot> hook_root;
            if (!hook_root_path_.empty()) {
                hook_root.emplace(hook_root_path_);
            }
            run_hook(hooks_, action, cancellation, hook_root ? &*hook_root : nullptr, hook_policy_);
            return;
        }
        case BackupRunActionKind::CreateSnapshot:
            create_local_snapshot(btrfs_, fs_effects_, source_plan, run_plan, local_root_.get());
            return;
        case BackupRunActionKind::VerifyReceived:
            verify_received(btrfs_, source_plan, local_root_.get(), target_root_.get());
            return;
        case BackupRunActionKind::CommitReceived:
            commit_received(btrfs_, fs_effects_, source_plan, local_root_.get(), target_root_.get());
            return;
        case BackupRunActionKind::ApplyRemoteRetention:
            apply_retention(btrfs_, source_plan.remote_retention, target_root_.get());
            return;
        case BackupRunActionKind::ApplyLocalRetention:
            apply_retention(btrfs_, source_plan.local_retention, local_root_.get());
            return;
        case BackupRunActionKind::CleanupSource:
            cleanup_source(btrfs_, fs_effects_, source_plan, target_root_.get());
            return;
        case BackupRunActionKind::SendReceive:
            prepare_send_receive(fs_effects_, source_plan, target_root_.get());
            return;
        case BackupRunActionKind::SelectParent:
            return;
    }
}

} // namespace btrfsbackup
