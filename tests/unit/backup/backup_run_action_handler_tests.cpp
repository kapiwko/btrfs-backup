// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <fcntl.h>
#include <sys/stat.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <backup/action_handlers/backup_run_action_handler.hpp>
#include <backup/action_handlers/hook_action_handler.hpp>
#include <backup/ports/btrfs_operations.hpp>
#include <backup/ports/command_runner.hpp>
#include <backup/ports/filesystem.hpp>
#include <backup/action_handlers/recovery_action_handler.hpp>
#include <backup/action_handlers/repository_action_handler.hpp>
#include <backup/action_handlers/retention_action_handler.hpp>
#include <backup/action_handlers/snapshot_action_handler.hpp>
#include <backup/action_handlers/transfer_action_handler.hpp>
#include <platform/linux/safe_directory_root.hpp>

#include "support/validation_test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

template <class Handler, class Action>
concept HandlesAction = requires(Handler& handler, const Action& action) {
    handler.handle(action);
};

template <class Handler>
concept HandlesHookAction = requires(
    Handler& handler,
    const btrfsbackup::RunHookAction& action,
    const btrfsbackup::ProfileId& profile_id,
    btrfsbackup::CancellationToken& cancellation
) {
    handler.handle(action, profile_id, cancellation);
};

static_assert(HandlesAction<btrfsbackup::SnapshotActionHandler, btrfsbackup::CreateSnapshotAction>);
static_assert(!HandlesAction<btrfsbackup::SnapshotActionHandler, btrfsbackup::RecoverPendingAction>);
static_assert(HandlesAction<btrfsbackup::RecoveryActionHandler, btrfsbackup::RecoverPendingAction>);
static_assert(!HandlesAction<btrfsbackup::RecoveryActionHandler, btrfsbackup::CreateSnapshotAction>);
static_assert(HandlesAction<btrfsbackup::RetentionActionHandler, btrfsbackup::ApplyRemoteRetentionAction>);
static_assert(HandlesAction<btrfsbackup::RetentionActionHandler, btrfsbackup::ApplyLocalRetentionAction>);
static_assert(!HandlesAction<btrfsbackup::RetentionActionHandler, btrfsbackup::CleanupSourceAction>);
static_assert(HandlesAction<btrfsbackup::RepositoryActionHandler, btrfsbackup::CleanupIncomingAction>);
static_assert(HandlesAction<btrfsbackup::RepositoryActionHandler, btrfsbackup::VerifyReceivedAction>);
static_assert(HandlesAction<btrfsbackup::RepositoryActionHandler, btrfsbackup::CommitReceivedAction>);
static_assert(HandlesAction<btrfsbackup::RepositoryActionHandler, btrfsbackup::CleanupSourceAction>);
static_assert(!HandlesAction<btrfsbackup::RepositoryActionHandler, btrfsbackup::SendReceiveAction>);
static_assert(HandlesAction<btrfsbackup::TransferActionHandler, btrfsbackup::SendReceiveAction>);
static_assert(!HandlesAction<btrfsbackup::TransferActionHandler, btrfsbackup::CommitReceivedAction>);
static_assert(HandlesHookAction<btrfsbackup::HookActionHandler>);
static_assert(!HandlesAction<btrfsbackup::HookActionHandler, btrfsbackup::RunHookAction>);

std::string action_path(const std::string& prefix, const fs::path& path) {
    return prefix + ":" + path.string();
}

class FakeBtrfsOperations final : public btrfsbackup::IBtrfsOperations {
  public:
    std::map<std::string, btrfsbackup::SnapshotMetadata> metadata_by_path;
    std::vector<std::string> subvolumes;
    std::vector<std::string> calls;
    std::optional<fs::path> delete_failure_path;

    bool is_subvolume(const fs::path& path) override {
        calls.push_back(action_path("is", path));
        for (const std::string& subvolume : subvolumes) {
            if (subvolume == path.string()) {
                return true;
            }
        }
        return false;
    }

    std::optional<btrfsbackup::SnapshotMetadata> read_snapshot_metadata(const fs::path& path) override {
        calls.push_back(action_path("metadata", path));
        auto it = metadata_by_path.find(path.string());
        if (it == metadata_by_path.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    void create_readonly_snapshot(const fs::path& source, const fs::path& target) override {
        calls.push_back("snapshot:" + source.string() + "->" + target.string());
    }

    void delete_subvolume(const fs::path& path) override {
        calls.push_back(action_path("delete", path));
        if (delete_failure_path == path) {
            throw btrfsbackup::ValidationError("injected subvolume delete failure");
        }
    }
};

class FakeFileSystem final : public btrfsbackup::IFileSystem {
  public:
    std::vector<fs::path> directories;
    std::map<std::string, std::vector<fs::path>> directory_entries;
    std::vector<std::string> calls;

    bool exists(const fs::path& path) override {
        calls.push_back(action_path("exists", path));
        if (directory_entries.contains(path.string())) {
            return true;
        }
        for (const fs::path& directory : directories) {
            if (directory == path) {
                return true;
            }
        }
        return false;
    }

    bool is_directory(const fs::path& path) override {
        calls.push_back(action_path("is-directory", path));
        return directory_entries.contains(path.string());
    }

    void create_directories(const fs::path& path) override {
        calls.push_back(action_path("mkdir", path));
        directories.push_back(path);
    }

    void remove_file(const fs::path& path) override {
        calls.push_back(action_path("remove-file", path));
    }

    void remove_directory(const fs::path& path) override {
        calls.push_back(action_path("rmdir", path));
    }

    void remove_tree(const fs::path& path) override {
        calls.push_back(action_path("remove-tree", path));
    }

    void rename_path(const fs::path& source, const fs::path& target) override {
        calls.push_back("rename:" + source.string() + "->" + target.string());
    }

    std::vector<fs::path> list_directory(const fs::path& path) override {
        calls.push_back(action_path("list", path));
        return directory_entries[path.string()];
    }
};

class FakeCommandRunner final : public btrfsbackup::ICommandRunner {
  public:
    std::vector<std::vector<std::string>> calls;
    int exit_code = 0;
    bool cancelled = false;
    bool timed_out = false;
    bool inherited_fds_were_open = false;
    std::optional<btrfsbackup::ControlledCommandOptions> controlled_options;

    btrfsbackup::CommandResult run(const std::vector<std::string>& argv) override {
        calls.push_back(argv);
        return btrfsbackup::CommandResult{
            .exit_code = exit_code,
            .output = {},
        };
    }

    btrfsbackup::CommandResult run_controlled(
        const std::vector<std::string>& argv,
        const btrfsbackup::ControlledCommandOptions& options
    ) override {
        controlled_options = options;
        inherited_fds_were_open = !options.inherited_fds.empty() && std::all_of(options.inherited_fds.begin(), options.inherited_fds.end(), [](int fd) {
            return fcntl(fd, F_GETFD) >= 0;
        });
        btrfsbackup::CommandResult result = run(argv);
        result.cancelled = cancelled;
        result.timed_out = timed_out;
        return result;
    }
};

class ActionHandlerFixture final : public btrfsbackup::IBackupRunActionHandler {
  public:
    ActionHandlerFixture(FakeBtrfsOperations& btrfs, FakeFileSystem& filesystem)
        : snapshots_(btrfs, filesystem),
          recovery_(btrfs),
          retention_(btrfs),
          hooks_(fallback_commands_),
          repository_(btrfs, filesystem),
          transfers_(filesystem),
          dispatcher_(snapshots_, recovery_, retention_, hooks_, repository_, transfers_) {
    }

    ActionHandlerFixture(
        FakeBtrfsOperations& btrfs,
        FakeFileSystem& filesystem,
        FakeCommandRunner& commands
    )
        : snapshots_(btrfs, filesystem),
          recovery_(btrfs),
          retention_(btrfs),
          hooks_(commands),
          repository_(btrfs, filesystem),
          transfers_(filesystem),
          dispatcher_(snapshots_, recovery_, retention_, hooks_, repository_, transfers_) {
    }

    ActionHandlerFixture(
        FakeBtrfsOperations& btrfs,
        FakeFileSystem& filesystem,
        FakeCommandRunner& commands,
        const fs::path& target_root
    )
        : snapshots_(btrfs, filesystem, std::make_unique<btrfsbackup::SafeDirectoryRoot>("/")),
          recovery_(
              btrfs,
              std::make_unique<btrfsbackup::SafeDirectoryRoot>("/"),
              std::make_unique<btrfsbackup::SafeDirectoryRoot>(target_root)
          ),
          retention_(
              btrfs,
              std::make_unique<btrfsbackup::SafeDirectoryRoot>("/"),
              std::make_unique<btrfsbackup::SafeDirectoryRoot>(target_root)
          ),
          hooks_(commands, btrfsbackup::trusted_hook_directory, {}),
          repository_(
              btrfs,
              filesystem,
              std::make_unique<btrfsbackup::SafeDirectoryRoot>("/"),
              std::make_unique<btrfsbackup::SafeDirectoryRoot>(target_root)
          ),
          transfers_(filesystem, std::make_unique<btrfsbackup::SafeDirectoryRoot>(target_root)),
          dispatcher_(snapshots_, recovery_, retention_, hooks_, repository_, transfers_) {
    }

    ActionHandlerFixture(
        FakeBtrfsOperations& btrfs,
        FakeFileSystem& filesystem,
        FakeCommandRunner& commands,
        const fs::path& target_root,
        const fs::path& hook_root,
        const btrfsbackup::TrustedExecutablePolicy& hook_policy
    )
        : snapshots_(btrfs, filesystem, std::make_unique<btrfsbackup::SafeDirectoryRoot>("/")),
          recovery_(
              btrfs,
              std::make_unique<btrfsbackup::SafeDirectoryRoot>("/"),
              std::make_unique<btrfsbackup::SafeDirectoryRoot>(target_root)
          ),
          retention_(
              btrfs,
              std::make_unique<btrfsbackup::SafeDirectoryRoot>("/"),
              std::make_unique<btrfsbackup::SafeDirectoryRoot>(target_root)
          ),
          hooks_(commands, hook_root, hook_policy),
          repository_(
              btrfs,
              filesystem,
              std::make_unique<btrfsbackup::SafeDirectoryRoot>("/"),
              std::make_unique<btrfsbackup::SafeDirectoryRoot>(target_root)
          ),
          transfers_(filesystem, std::make_unique<btrfsbackup::SafeDirectoryRoot>(target_root)),
          dispatcher_(snapshots_, recovery_, retention_, hooks_, repository_, transfers_) {
    }

    void handle(
        const btrfsbackup::BackupRunAction& action,
        const btrfsbackup::BackupRunPlan& plan,
        btrfsbackup::CancellationToken& cancellation
    ) override {
        dispatcher_.handle(action, plan, cancellation);
    }

  private:
    FakeCommandRunner fallback_commands_;
    btrfsbackup::SnapshotActionHandler snapshots_;
    btrfsbackup::RecoveryActionHandler recovery_;
    btrfsbackup::RetentionActionHandler retention_;
    btrfsbackup::HookActionHandler hooks_;
    btrfsbackup::RepositoryActionHandler repository_;
    btrfsbackup::TransferActionHandler transfers_;
    btrfsbackup::BackupRunActionHandler dispatcher_;
};

btrfsbackup::BackupRunPlan run_plan() {
    return btrfsbackup::BackupRunPlan{
        .profile_id = btrfsbackup::ProfileId{"default"},
        .run_id = btrfsbackup::RunId{"20260823T120000Z-123-456"},
    };
}

btrfsbackup::BackupSourceRunPlan source_plan(const fs::path& root) {
    btrfsbackup::BackupSourceRunPlan source{.source_id = btrfsbackup::SourceId{"root"}};
    source.source_subvolume = root / "source";
    source.local_snapshot_dir = root / "local";
    source.remote_snapshot_dir = root / "remote";
    source.incoming_source_root = root / "incoming" / "root";
    source.incoming_run_dir = source.incoming_source_root / "run-1";
    source.local_snapshot_path = source.local_snapshot_dir / "root-2026-08-23T120000Z";
    source.received_snapshot_path = source.incoming_run_dir / "root-2026-08-23T120000Z";
    source.final_remote_snapshot_path = source.remote_snapshot_dir / "root-2026-08-23T120000Z";
    source.recovery.marker_path = root / "state" / "pending-root";
    return source;
}

btrfsbackup::BackupRunAction action(
    btrfsbackup::BackupRunActionKind kind,
    const btrfsbackup::BackupSourceRunPlan& source
) {
    using namespace btrfsbackup;
    switch (kind) {
    case BackupRunActionKind::RecoverPending:
        return RecoverPendingAction{source.source_id, source.recovery};
    case BackupRunActionKind::CleanupIncoming:
        return CleanupIncomingAction{source.source_id, source.incoming_source_root};
    case BackupRunActionKind::CreateSnapshot:
        return CreateSnapshotAction{
            source.source_id,
            source.source_subvolume,
            source.local_snapshot_dir,
            source.local_snapshot_path,
            source.final_remote_snapshot_path,
            source.recovery.marker_path.parent_path(),
            run_plan().run_id,
        };
    case BackupRunActionKind::SelectParent:
        return SelectParentAction{source.source_id, std::nullopt};
    case BackupRunActionKind::SendReceive:
        return SendReceiveAction{
            source.source_id,
            source.local_snapshot_path,
            std::nullopt,
            source.remote_snapshot_dir,
            source.incoming_run_dir,
        };
    case BackupRunActionKind::VerifyReceived:
        return VerifyReceivedAction{
            source.source_id,
            source.local_snapshot_path,
            source.received_snapshot_path,
        };
    case BackupRunActionKind::CommitReceived:
        return CommitReceivedAction{
            source.source_id,
            source.local_snapshot_path,
            source.received_snapshot_path,
            source.final_remote_snapshot_path,
        };
    case BackupRunActionKind::ApplyRemoteRetention:
        return ApplyRemoteRetentionAction{source.source_id, source.remote_retention};
    case BackupRunActionKind::ApplyLocalRetention:
        return ApplyLocalRetentionAction{source.source_id, source.local_retention};
    case BackupRunActionKind::CleanupSource:
        return CleanupSourceAction{
            source.source_id,
            source.received_snapshot_path,
            source.incoming_run_dir,
            source.recovery.marker_path,
            source.recovery.marker_path.parent_path(),
        };
    case BackupRunActionKind::BeforeSnapshotHook:
    case BackupRunActionKind::AfterSnapshotHook:
        break;
    }
    throw std::logic_error("hook action requires hook_action");
}

btrfsbackup::BackupRunAction hook_action(btrfsbackup::HookPhase phase) {
    return btrfsbackup::RunHookAction{
        btrfsbackup::SourceId{"root"},
        phase,
        btrfsbackup::ProfileHookCommand{
            .program = "/etc/btrfs-backup/hooks.d/prepare-backup",
            .arguments = {"--source", "root"},
            .timeout = std::chrono::seconds{300},
        },
    };
}

void handle_action(
    btrfsbackup::IBackupRunActionHandler& handler,
    const btrfsbackup::BackupRunAction& action
) {
    btrfsbackup::CancellationToken cancellation;
    handler.handle(action, run_plan(), cancellation);
}

void test_create_snapshot_writes_pending_marker_and_verifies_readonly_snapshot() {
    fs::path root = test_helpers::test_root("backup-run-action-handler", "create-snapshot");
    btrfsbackup::BackupSourceRunPlan source = source_plan(root);
    FakeBtrfsOperations btrfs;
    btrfs.metadata_by_path[source.local_snapshot_path.string()] = btrfsbackup::SnapshotMetadata{
        .is_subvolume = true,
        .readonly = true,
        .uuid = "local-uuid",
    };
    FakeFileSystem fs_effects;
    ActionHandlerFixture handler(btrfs, fs_effects);

    handle_action(handler, action(btrfsbackup::BackupRunActionKind::CreateSnapshot, source));

    test_helpers::expect_eq("mkdir local snapshot dir", fs_effects.calls.at(0), action_path("mkdir", source.local_snapshot_dir));
    test_helpers::expect_eq("snapshot call", btrfs.calls.at(0), "snapshot:" + source.source_subvolume.string() + "->" + source.local_snapshot_path.string());
    test_helpers::expect_eq("metadata call", btrfs.calls.at(1), action_path("metadata", source.local_snapshot_path));
    test_helpers::expect_true("pending marker exists", fs::is_regular_file(source.recovery.marker_path), "pending marker should be written");
}

void test_cleanup_incoming_deletes_subvolumes_and_plain_paths() {
    fs::path root = test_helpers::test_root("backup-run-action-handler", "cleanup-incoming");
    btrfsbackup::BackupSourceRunPlan source = source_plan(root);
    fs::path subvolume = source.incoming_source_root / "old-subvol";
    fs::path directory = source.incoming_source_root / "old-dir";
    fs::path nested_subvolume = directory / "received-subvol";
    FakeBtrfsOperations btrfs;
    btrfs.subvolumes = {subvolume.string(), nested_subvolume.string()};
    FakeFileSystem fs_effects;
    fs_effects.directories = {directory, subvolume, nested_subvolume};
    fs_effects.directory_entries[source.incoming_source_root.string()] = {directory, subvolume};
    fs_effects.directory_entries[directory.string()] = {nested_subvolume};
    ActionHandlerFixture handler(btrfs, fs_effects);

    handle_action(handler, action(btrfsbackup::BackupRunActionKind::CleanupIncoming, source));

    test_helpers::expect_true("list incoming", std::find(fs_effects.calls.begin(), fs_effects.calls.end(), action_path("list", source.incoming_source_root)) != fs_effects.calls.end(), "incoming root should be listed");
    test_helpers::expect_true("delete nested subvolume", std::find(btrfs.calls.begin(), btrfs.calls.end(), action_path("delete", nested_subvolume)) != btrfs.calls.end(), "nested subvolume should be deleted before removing its parent directory");
    test_helpers::expect_true("delete plain dir", std::find(fs_effects.calls.begin(), fs_effects.calls.end(), action_path("rmdir", directory)) != fs_effects.calls.end(), "plain directory should be removed after its contents");
    test_helpers::expect_true("delete subvolume", std::find(btrfs.calls.begin(), btrfs.calls.end(), action_path("delete", subvolume)) != btrfs.calls.end(), "subvolume should be deleted with btrfs");
}

void test_production_cleanup_rejects_incoming_symlink_escape() {
    fs::path root = test_helpers::test_root("backup-run-action-handler", "cleanup-symlink");
    fs::path target = root / "target";
    fs::path outside = root / "outside";
    fs::create_directories(target / ".incoming");
    fs::create_directories(outside);
    test_helpers::write_file(outside / "sentinel", "keep\n");
    fs::create_directory_symlink(outside, target / ".incoming" / "root");

    btrfsbackup::BackupSourceRunPlan source = source_plan(root);
    source.incoming_source_root = target / ".incoming" / "root";
    source.incoming_run_dir = source.incoming_source_root / "run-1";
    FakeBtrfsOperations btrfs;
    FakeFileSystem fs_effects;
    FakeCommandRunner hooks;
    ActionHandlerFixture handler(btrfs, fs_effects, hooks, target);

    test_helpers::expect_validation_error("production cleanup symlink", [&] { handle_action(handler, action(btrfsbackup::BackupRunActionKind::CleanupIncoming, source)); }, "Too many levels of symbolic links");
    test_helpers::expect_true(
        "production cleanup outside preserved",
        fs::is_regular_file(outside / "sentinel"),
        "cleanup followed incoming symlink outside target"
    );

    fs::remove_all(root);
}

void test_verify_commit_retention_and_cleanup_use_existing_helpers() {
    fs::path root = test_helpers::test_root("backup-run-action-handler", "commit-cleanup");
    btrfsbackup::BackupSourceRunPlan source = source_plan(root);
    source.local_retention.delete_snapshots = {
        btrfsbackup::SnapshotInfo{.path = root / "local" / "old"},
    };
    source.remote_retention.delete_snapshots = {
        btrfsbackup::SnapshotInfo{.path = root / "remote" / "old"},
    };

    FakeBtrfsOperations btrfs;
    btrfs.subvolumes = {source.received_snapshot_path.string()};
    btrfs.metadata_by_path[source.local_snapshot_path.string()] = btrfsbackup::SnapshotMetadata{
        .is_subvolume = true,
        .readonly = true,
        .uuid = "local-uuid",
    };
    btrfs.metadata_by_path[source.received_snapshot_path.string()] = btrfsbackup::SnapshotMetadata{
        .is_subvolume = true,
        .readonly = true,
        .received_uuid = "local-uuid",
    };
    btrfs.metadata_by_path[source.final_remote_snapshot_path.string()] = btrfsbackup::SnapshotMetadata{
        .is_subvolume = true,
        .readonly = true,
        .received_uuid = "local-uuid",
    };

    FakeFileSystem fs_effects;
    fs_effects.directories = {source.received_snapshot_path, source.incoming_run_dir};
    fs_effects.directory_entries[source.incoming_run_dir.string()] = {};
    ActionHandlerFixture handler(btrfs, fs_effects);

    handle_action(handler, action(btrfsbackup::BackupRunActionKind::VerifyReceived, source));
    handle_action(handler, action(btrfsbackup::BackupRunActionKind::CommitReceived, source));
    handle_action(handler, action(btrfsbackup::BackupRunActionKind::ApplyRemoteRetention, source));
    handle_action(handler, action(btrfsbackup::BackupRunActionKind::ApplyLocalRetention, source));
    handle_action(handler, action(btrfsbackup::BackupRunActionKind::CleanupSource, source));

    test_helpers::expect_true("commit snapshot", std::find(btrfs.calls.begin(), btrfs.calls.end(), "snapshot:" + source.received_snapshot_path.string() + "->" + source.final_remote_snapshot_path.string()) != btrfs.calls.end(), "commit should snapshot received subvolume");
    test_helpers::expect_true("remote retention delete", std::find(btrfs.calls.begin(), btrfs.calls.end(), action_path("delete", root / "remote" / "old")) != btrfs.calls.end(), "remote retention should delete planned snapshot");
    test_helpers::expect_true("local retention delete", std::find(btrfs.calls.begin(), btrfs.calls.end(), action_path("delete", root / "local" / "old")) != btrfs.calls.end(), "local retention should delete planned snapshot");
    test_helpers::expect_true("cleanup received", std::find(btrfs.calls.begin(), btrfs.calls.end(), action_path("delete", source.received_snapshot_path)) != btrfs.calls.end(), "cleanup should delete received subvolume");
}

void test_send_receive_prepares_remote_and_incoming_directories() {
    fs::path root = test_helpers::test_root("backup-run-action-handler", "send-receive");
    btrfsbackup::BackupSourceRunPlan source = source_plan(root);
    FakeBtrfsOperations btrfs;
    FakeFileSystem fs_effects;
    ActionHandlerFixture handler(btrfs, fs_effects);

    handle_action(handler, action(btrfsbackup::BackupRunActionKind::SendReceive, source));

    test_helpers::expect_true("remote dir", std::find(fs_effects.calls.begin(), fs_effects.calls.end(), action_path("mkdir", source.remote_snapshot_dir)) != fs_effects.calls.end(), "remote snapshot directory should be created");
    test_helpers::expect_true("incoming run dir", std::find(fs_effects.calls.begin(), fs_effects.calls.end(), action_path("mkdir", source.incoming_run_dir)) != fs_effects.calls.end(), "incoming run directory should be created before receive");
}

void test_pending_recovery_deletes_invalid_remote_snapshot_first() {
    fs::path root = test_helpers::test_root("backup-run-action-handler", "recover-invalid-commit");
    btrfsbackup::BackupSourceRunPlan source = source_plan(root);
    source.recovery.delete_remote_snapshot = true;
    source.recovery.remote_snapshot_path = source.final_remote_snapshot_path;
    source.recovery.delete_local_snapshot = true;
    source.recovery.local_snapshot_path = source.local_snapshot_path;
    FakeBtrfsOperations btrfs;
    FakeFileSystem fs_effects;
    ActionHandlerFixture handler(btrfs, fs_effects);

    handle_action(handler, action(btrfsbackup::BackupRunActionKind::RecoverPending, source));

    test_helpers::expect_eq("recovery delete count", std::to_string(btrfs.calls.size()), "2");
    test_helpers::expect_eq("recovery remote first", btrfs.calls.at(0), action_path("delete", source.final_remote_snapshot_path));
    test_helpers::expect_eq("recovery local second", btrfs.calls.at(1), action_path("delete", source.local_snapshot_path));
}

void test_failed_remote_recovery_keeps_local_snapshot_and_marker() {
    fs::path root = test_helpers::test_root("backup-run-action-handler", "recover-invalid-commit-fails");
    btrfsbackup::BackupSourceRunPlan source = source_plan(root);
    source.recovery.delete_remote_snapshot = true;
    source.recovery.remote_snapshot_path = source.final_remote_snapshot_path;
    source.recovery.delete_local_snapshot = true;
    source.recovery.local_snapshot_path = source.local_snapshot_path;
    source.recovery.clear_marker = true;
    btrfsbackup::write_pending_marker(
        root / "state",
        btrfsbackup::PendingMarker{
            .source_name = std::string(source.source_id.value()),
            .local_snapshot_path = source.local_snapshot_path.string(),
            .final_snapshot_path = source.final_remote_snapshot_path.string(),
            .run_id = "run-1",
            .timestamp = "2026-08-23T12:00:00Z",
        }
    );

    FakeBtrfsOperations btrfs;
    btrfs.delete_failure_path = source.final_remote_snapshot_path;
    FakeFileSystem fs_effects;
    ActionHandlerFixture handler(btrfs, fs_effects);

    test_helpers::expect_validation_error("failed recovery delete", [&] { handle_action(handler, action(btrfsbackup::BackupRunActionKind::RecoverPending, source)); }, "injected subvolume delete failure");

    test_helpers::expect_eq("failed recovery delete count", std::to_string(btrfs.calls.size()), "1");
    test_helpers::expect_eq("failed recovery stops at remote", btrfs.calls.at(0), action_path("delete", source.final_remote_snapshot_path));
    test_helpers::expect_true("failed recovery keeps marker", fs::is_regular_file(source.recovery.marker_path), "pending marker must remain after failed cleanup");
    fs::remove_all(root);
}

void test_hook_actions_use_command_runner_argv() {
    fs::path root = test_helpers::test_root("backup-run-action-handler", "hooks");
    btrfsbackup::BackupSourceRunPlan source = source_plan(root);
    FakeBtrfsOperations btrfs;
    FakeFileSystem fs_effects;
    FakeCommandRunner hooks;
    ActionHandlerFixture handler(btrfs, fs_effects, hooks);

    handle_action(handler, hook_action(btrfsbackup::HookPhase::BeforeSnapshot));

    test_helpers::expect_eq("hook call count", std::to_string(hooks.calls.size()), "1");
    test_helpers::expect_eq("hook program", hooks.calls.at(0).at(0), "/etc/btrfs-backup/hooks.d/prepare-backup");
    test_helpers::expect_eq("hook arg 1", hooks.calls.at(0).at(1), "--source");
    test_helpers::expect_eq("hook arg 2", hooks.calls.at(0).at(2), "root");
    test_helpers::expect_eq(
        "hook timeout forwarded",
        std::to_string(hooks.controlled_options->timeout.count()),
        std::to_string(std::chrono::minutes(5).count() * 60 * 1000)
    );
    test_helpers::expect_eq(
        "hook profile environment",
        hooks.controlled_options->environment.at("BTRFS_BACKUP_PROFILE_ID"),
        "default"
    );
    test_helpers::expect_eq(
        "hook source environment",
        hooks.controlled_options->environment.at("BTRFS_BACKUP_SOURCE_ID"),
        "root"
    );
}

void test_production_hook_uses_pinned_trusted_descriptor() {
    fs::path root = test_helpers::test_root("backup-run-action-handler", "trusted-hook");
    fs::path hook_root = root / "hooks.d";
    fs::create_directories(hook_root);
    fs::path program = hook_root / "prepare-backup";
    test_helpers::write_file(program, "#!/bin/sh\nexit 0\n");
    chmod(program.c_str(), 0700);

    btrfsbackup::BackupSourceRunPlan source = source_plan(root);
    FakeBtrfsOperations btrfs;
    FakeFileSystem fs_effects;
    FakeCommandRunner hooks;
    ActionHandlerFixture handler(
        btrfs,
        fs_effects,
        hooks,
        root,
        hook_root,
        {.allow_current_user_owner = true, .verify_parent_directories = false}
    );
    btrfsbackup::BackupRunAction trusted_hook = hook_action(btrfsbackup::HookPhase::BeforeSnapshot);
    std::get<btrfsbackup::RunHookAction>(trusted_hook).hook.program = program.string();

    handle_action(handler, trusted_hook);

    test_helpers::expect_contains("pinned hook argv", hooks.calls.at(0).at(0), "/proc/self/fd/");
    test_helpers::expect_true(
        "pinned hook descriptor inherited",
        hooks.inherited_fds_were_open,
        "hook descriptor was not open while invoking the command runner"
    );
    test_helpers::expect_eq(
        "pinned hook inherited descriptor count",
        std::to_string(hooks.controlled_options->inherited_fds.size()),
        "1"
    );

    fs::remove_all(root);
}

void test_hook_failure_is_reported_as_system_operation_error() {
    fs::path root = test_helpers::test_root("backup-run-action-handler", "hook-failure");
    btrfsbackup::BackupSourceRunPlan source = source_plan(root);
    FakeBtrfsOperations btrfs;
    FakeFileSystem fs_effects;
    FakeCommandRunner hooks;
    hooks.exit_code = 42;
    ActionHandlerFixture handler(btrfs, fs_effects, hooks);

    try {
        handle_action(handler, hook_action(btrfsbackup::HookPhase::AfterSnapshot));
        test_helpers::expect_true("hook failure type", false, "hook failure should fail the action");
    } catch (const btrfsbackup::SystemOperationError& error) {
        test_helpers::expect_contains("hook failure message", error.what(), "hook failed");
    }
}

void test_hook_timeout_has_stable_error_code() {
    fs::path root = test_helpers::test_root("backup-run-action-handler", "hook-timeout");
    btrfsbackup::BackupSourceRunPlan source = source_plan(root);
    FakeBtrfsOperations btrfs;
    FakeFileSystem fs_effects;
    FakeCommandRunner hooks;
    hooks.timed_out = true;
    ActionHandlerFixture handler(btrfs, fs_effects, hooks);
    btrfsbackup::BackupRunAction timed_hook = hook_action(btrfsbackup::HookPhase::BeforeSnapshot);
    std::get<btrfsbackup::RunHookAction>(timed_hook).hook.timeout = std::chrono::seconds{17};

    try {
        handle_action(handler, timed_hook);
        test_helpers::expect_true("hook timeout throws", false, "timeout should fail the action");
    } catch (const btrfsbackup::CodedOperationError& error) {
        test_helpers::expect_eq("hook timeout code", error.error_code, "hook.before_snapshot_timeout");
        test_helpers::expect_contains("hook timeout message", error.what(), "17 seconds");
    }
}

void test_hook_cancellation_is_not_reported_as_hook_failure() {
    fs::path root = test_helpers::test_root("backup-run-action-handler", "hook-cancel");
    btrfsbackup::BackupSourceRunPlan source = source_plan(root);
    FakeBtrfsOperations btrfs;
    FakeFileSystem fs_effects;
    FakeCommandRunner hooks;
    hooks.cancelled = true;
    ActionHandlerFixture handler(btrfs, fs_effects, hooks);

    bool cancelled = false;
    try {
        handle_action(handler, hook_action(btrfsbackup::HookPhase::AfterSnapshot));
    } catch (const btrfsbackup::OperationCancelledError&) {
        cancelled = true;
    }
    test_helpers::expect_true("hook cancellation type", cancelled, "hook cancellation should have a distinct result");
}

} // namespace

int main() {
    test_create_snapshot_writes_pending_marker_and_verifies_readonly_snapshot();
    test_cleanup_incoming_deletes_subvolumes_and_plain_paths();
    test_production_cleanup_rejects_incoming_symlink_escape();
    test_verify_commit_retention_and_cleanup_use_existing_helpers();
    test_send_receive_prepares_remote_and_incoming_directories();
    test_pending_recovery_deletes_invalid_remote_snapshot_first();
    test_failed_remote_recovery_keeps_local_snapshot_and_marker();
    test_hook_actions_use_command_runner_argv();
    test_production_hook_uses_pinned_trusted_descriptor();
    test_hook_failure_is_reported_as_system_operation_error();
    test_hook_timeout_has_stable_error_code();
    test_hook_cancellation_is_not_reported_as_hook_failure();

    return test_helpers::finish("backup run action handler tests");
}
