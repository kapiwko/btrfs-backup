// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/backup_run_action_effects.hpp>

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include <config/errors.hpp>
#include <platform/linux/safe_directory_root.hpp>
#include <platform/linux/trusted_executable.hpp>
#include <state/run_state.hpp>
#include <backup/snapshot_transfer.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

BackupRunActionEffects::~BackupRunActionEffects() = default;

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
    const RecoverPendingAction& action,
    const SafeDirectoryRoot* local_root,
    const SafeDirectoryRoot* target_root
) {
    if (action.recovery.delete_remote_snapshot) {
        if (target_root == nullptr) {
            btrfs.delete_subvolume(action.recovery.remote_snapshot_path);
        } else {
            btrfs.delete_subvolume_beneath(*target_root, action.recovery.remote_snapshot_path);
        }
    }
    if (action.recovery.delete_local_snapshot) {
        if (local_root == nullptr) {
            btrfs.delete_subvolume(action.recovery.local_snapshot_path);
        } else {
            btrfs.delete_subvolume_beneath(*local_root, action.recovery.local_snapshot_path);
        }
    }
    if (action.recovery.clear_marker) {
        clear_pending_marker(action.recovery.marker_path, action.recovery.marker_path.parent_path());
    }
}

void create_local_snapshot(
    IBtrfsOperations& btrfs,
    IFileSystem& fs_effects,
    const CreateSnapshotAction& action,
    const SafeDirectoryRoot* local_root
) {
    if (local_root == nullptr) {
        fs_effects.create_directories(action.snapshot_directory);
    } else {
        local_root->ensure_directory(action.snapshot_directory);
    }
    write_pending_marker(
        action.profile_state_directory,
        PendingMarker{
            .source_name = action.source_id.value,
            .local_snapshot_path = action.snapshot.string(),
            .final_snapshot_path = action.final_remote_snapshot.string(),
            .run_id = action.run_id.value,
            .timestamp = current_utc_iso_timestamp(),
        }
    );

    if (local_root == nullptr) {
        btrfs.create_readonly_snapshot(action.source, action.snapshot);
    } else {
        btrfs.create_readonly_snapshot_beneath(
            *local_root,
            action.source,
            *local_root,
            action.snapshot
        );
    }
    SnapshotMetadata metadata = require_snapshot_metadata(
        btrfs,
        action.snapshot,
        "New local snapshot metadata is missing",
        local_root
    );
    if (!metadata.is_subvolume || !metadata.readonly) {
        throw ValidationError("New local snapshot is not readonly: " + action.snapshot.string());
    }
}

void verify_received(
    IBtrfsOperations& btrfs,
    const VerifyReceivedAction& action,
    const SafeDirectoryRoot* local_root,
    const SafeDirectoryRoot* target_root
) {
    SnapshotMetadata local = require_snapshot_metadata(
        btrfs,
        action.local_snapshot,
        "Local snapshot metadata is missing",
        local_root
    );
    SnapshotMetadata received = require_snapshot_metadata(
        btrfs,
        action.received_snapshot,
        "Received snapshot metadata is missing",
        target_root
    );
    verify_received_snapshot(action.source_id.value, local, received);
}

void commit_received(
    IBtrfsOperations& btrfs,
    IFileSystem& fs_effects,
    const CommitReceivedAction& action,
    const SafeDirectoryRoot* local_root,
    const SafeDirectoryRoot* target_root
) {
    SnapshotMetadata local = require_snapshot_metadata(
        btrfs,
        action.local_snapshot,
        "Local snapshot metadata is missing",
        local_root
    );
    if (target_root == nullptr) {
        commit_received_snapshot(
            btrfs,
            fs_effects,
            action.received_snapshot,
            action.final_snapshot,
            local.uuid
        );
    } else {
        commit_received_snapshot_beneath(
            btrfs,
            *target_root,
            action.received_snapshot,
            action.final_snapshot,
            local.uuid
        );
    }
}

void prepare_send_receive(
    IFileSystem& fs_effects,
    const SendReceiveAction& action,
    const SafeDirectoryRoot* target_root
) {
    if (target_root == nullptr) {
        fs_effects.create_directories(action.remote_snapshot_directory);
        fs_effects.create_directories(action.incoming_run_directory);
    } else {
        target_root->ensure_directory(action.remote_snapshot_directory);
        target_root->ensure_directory(action.incoming_run_directory);
    }
}

void cleanup_source(
    IBtrfsOperations& btrfs,
    IFileSystem& fs_effects,
    const CleanupSourceAction& action,
    const SafeDirectoryRoot* target_root
) {
    cleanup_path(btrfs, fs_effects, action.received_snapshot, target_root);
    cleanup_incoming_run_dir(btrfs, fs_effects, action.incoming_run_directory, target_root);
    clear_pending_marker(action.pending_marker, action.profile_state_directory);
}

std::string hook_error_code(const RunHookAction& action, const std::string& suffix) {
    const std::string phase = action.phase == HookPhase::BeforeSnapshot
        ? "before_snapshot"
        : "after_snapshot";
    return "hook." + phase + "_" + suffix;
}

void run_hook(
    ICommandRunner* hooks,
    const RunHookAction& action,
    const ProfileId& profile_id,
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
                                                 .cancellation = &cancellation,
                                                 .timeout = std::chrono::seconds(action.hook.timeout_seconds),
                                                 .inherited_fds = inherited_fds,
                                                 .profile_id = profile_id,
                                                 .source_id = action.source_id,
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

template <class... Visitors>
struct Overloaded : Visitors... {
    using Visitors::operator()...;
};

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
    const BackupRunPlan& run_plan,
    CancellationToken& cancellation
) {
    std::visit(Overloaded{
                   [&](const RecoverPendingAction& typed_action) {
                       recover_pending(btrfs_, typed_action, local_root_.get(), target_root_.get());
                   },
                   [&](const CleanupIncomingAction& typed_action) {
                       cleanup_directory_contents(btrfs_, fs_effects_, typed_action.incoming_directory, target_root_.get());
                   },
                   [&](const RunHookAction& typed_action) {
                       std::optional<SafeDirectoryRoot> hook_root;
                       if (!hook_root_path_.empty()) {
                           hook_root.emplace(hook_root_path_);
                       }
                       run_hook(
                           hooks_,
                           typed_action,
                           run_plan.profile_id,
                           cancellation,
                           hook_root ? &*hook_root : nullptr,
                           hook_policy_
                       );
                   },
                   [&](const CreateSnapshotAction& typed_action) {
                       create_local_snapshot(btrfs_, fs_effects_, typed_action, local_root_.get());
                   },
                   [](const SelectParentAction&) {},
                   [&](const SendReceiveAction& typed_action) {
                       prepare_send_receive(fs_effects_, typed_action, target_root_.get());
                   },
                   [&](const VerifyReceivedAction& typed_action) {
                       verify_received(btrfs_, typed_action, local_root_.get(), target_root_.get());
                   },
                   [&](const CommitReceivedAction& typed_action) {
                       commit_received(btrfs_, fs_effects_, typed_action, local_root_.get(), target_root_.get());
                   },
                   [&](const ApplyRemoteRetentionAction& typed_action) {
                       apply_retention(btrfs_, typed_action.plan, target_root_.get());
                   },
                   [&](const ApplyLocalRetentionAction& typed_action) {
                       apply_retention(btrfs_, typed_action.plan, local_root_.get());
                   },
                   [&](const CleanupSourceAction& typed_action) {
                       cleanup_source(btrfs_, fs_effects_, typed_action, target_root_.get());
                   },
               },
               action);
}

} // namespace btrfsbackup
