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
#include <type_traits>
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
#include <backup/default_backup_run_action_handler_factory.hpp>

#include <platform/linux/safe_directory_root.hpp>
#include <platform/linux/posix_durable_file_operations.hpp>
#include <platform/linux/trusted_executable.hpp>
#include <state/file_pending_marker_store.hpp>
#include <state/run_state.hpp>

#include "support/fake_trusted_executable.hpp"
#include "support/fake_safe_directory.hpp"
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
    const btrfsbackup::backup::RunHookAction& action,
    const btrfsbackup::ProfileId& profile_id,
    btrfsbackup::CancellationToken& cancellation
) {
    handler.handle(action, profile_id, cancellation);
};

static_assert(HandlesAction<btrfsbackup::backup::SnapshotActionHandler, btrfsbackup::backup::CreateSnapshotAction>);
static_assert(!HandlesAction<btrfsbackup::backup::SnapshotActionHandler, btrfsbackup::backup::RecoverPendingAction>);
static_assert(HandlesAction<btrfsbackup::backup::RecoveryActionHandler, btrfsbackup::backup::RecoverPendingAction>);
static_assert(!HandlesAction<btrfsbackup::backup::RecoveryActionHandler, btrfsbackup::backup::CreateSnapshotAction>);
static_assert(HandlesAction<btrfsbackup::backup::RetentionActionHandler, btrfsbackup::backup::ApplyRemoteRetentionAction>);
static_assert(HandlesAction<btrfsbackup::backup::RetentionActionHandler, btrfsbackup::backup::ApplyLocalRetentionAction>);
static_assert(!HandlesAction<btrfsbackup::backup::RetentionActionHandler, btrfsbackup::backup::CleanupSourceAction>);
static_assert(HandlesAction<btrfsbackup::backup::RepositoryActionHandler, btrfsbackup::backup::CleanupIncomingAction>);
static_assert(HandlesAction<btrfsbackup::backup::RepositoryActionHandler, btrfsbackup::backup::VerifyReceivedAction>);
static_assert(HandlesAction<btrfsbackup::backup::RepositoryActionHandler, btrfsbackup::backup::CommitReceivedAction>);
static_assert(HandlesAction<btrfsbackup::backup::RepositoryActionHandler, btrfsbackup::backup::CleanupSourceAction>);
static_assert(!HandlesAction<btrfsbackup::backup::RepositoryActionHandler, btrfsbackup::backup::SendReceiveAction>);
static_assert(!std::is_constructible_v<
              btrfsbackup::backup::RepositoryActionHandler,
              btrfsbackup::backup::IBtrfsOperations&,
              btrfsbackup::backup::IFileSystem&,
              btrfsbackup::backup::IPendingMarkerStore&>);
static_assert(std::is_constructible_v<
              btrfsbackup::backup::RepositoryActionHandler,
              btrfsbackup::backup::IBtrfsOperations&,
              btrfsbackup::backup::IPendingMarkerStore&,
              btrfsbackup::backup::ISafeDirectoryRoot&,
              btrfsbackup::backup::ISafeDirectoryRoot&>);
static_assert(HandlesHookAction<btrfsbackup::backup::HookActionHandler>);
static_assert(!HandlesAction<btrfsbackup::backup::HookActionHandler, btrfsbackup::backup::RunHookAction>);

std::string action_path(const std::string& prefix, const fs::path& path) {
    return prefix + ":" + path.string();
}

class FakeBtrfsOperations final : public btrfsbackup::backup::IBtrfsOperations {
  public:
    std::map<std::string, btrfsbackup::backup::SnapshotMetadata> metadata_by_path;
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

    std::optional<btrfsbackup::backup::SnapshotMetadata> read_snapshot_metadata(const fs::path& path) override {
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

class FakeFileSystem final : public btrfsbackup::backup::IFileSystem {
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

class FakeCommandRunner final : public btrfsbackup::backup::ICommandRunner {
  public:
    std::vector<std::vector<std::string>> calls;
    int exit_code = 0;
    bool cancelled = false;
    bool timed_out = false;
    bool inherited_fds_were_open = false;
    std::optional<btrfsbackup::backup::ControlledCommandOptions> controlled_options;

    btrfsbackup::backup::CommandResult run(const std::vector<std::string>& argv) override {
        calls.push_back(argv);
        return btrfsbackup::backup::CommandResult{
            .exit_code = exit_code,
            .output = {},
        };
    }

    btrfsbackup::backup::CommandResult run_controlled(
        const std::vector<std::string>& argv,
        const btrfsbackup::backup::ControlledCommandOptions& options
    ) override {
        controlled_options = options;
        inherited_fds_were_open = !options.inherited_fds.empty() && std::all_of(options.inherited_fds.begin(), options.inherited_fds.end(), [](int fd) {
            return fcntl(fd, F_GETFD) >= 0;
        });
        btrfsbackup::backup::CommandResult result = run(argv);
        result.cancelled = cancelled;
        result.timed_out = timed_out;
        return result;
    }
};

class ActionHandlerFixture final : public btrfsbackup::backup::IBackupRunActionHandler {
  public:
    ActionHandlerFixture(FakeBtrfsOperations& btrfs, FakeFileSystem& filesystem)
        : pending_markers_(durable_files_),
          snapshots_(btrfs, filesystem, pending_markers_),
          recovery_(btrfs, pending_markers_),
          retention_(btrfs),
          hook_executables_(std::make_unique<test_support::FakeTrustedExecutableResolver>()),
          hooks_(fallback_commands_, *hook_executables_),
          local_repository_root_(std::make_unique<test_support::FakeSafeDirectoryRoot>("/", fs::path{}, false)),
          target_repository_root_(std::make_unique<test_support::FakeSafeDirectoryRoot>("/", fs::path{}, false)),
          repository_(btrfs, pending_markers_, *local_repository_root_, *target_repository_root_),
          dispatcher_(snapshots_, recovery_, retention_, hooks_, repository_) {
    }

    ActionHandlerFixture(
        FakeBtrfsOperations& btrfs,
        FakeFileSystem& filesystem,
        FakeCommandRunner& commands
    )
        : pending_markers_(durable_files_),
          snapshots_(btrfs, filesystem, pending_markers_),
          recovery_(btrfs, pending_markers_),
          retention_(btrfs),
          hook_executables_(std::make_unique<test_support::FakeTrustedExecutableResolver>()),
          hooks_(commands, *hook_executables_),
          local_repository_root_(std::make_unique<test_support::FakeSafeDirectoryRoot>("/", fs::path{}, false)),
          target_repository_root_(std::make_unique<test_support::FakeSafeDirectoryRoot>("/", fs::path{}, false)),
          repository_(btrfs, pending_markers_, *local_repository_root_, *target_repository_root_),
          dispatcher_(snapshots_, recovery_, retention_, hooks_, repository_) {
    }

    ActionHandlerFixture(
        FakeBtrfsOperations& btrfs,
        FakeFileSystem& filesystem,
        FakeCommandRunner& commands,
        const fs::path& target_root
    )
        : pending_markers_(durable_files_),
          snapshots_(btrfs, filesystem, pending_markers_, std::make_unique<btrfsbackup::platform::linux::SafeDirectoryRoot>("/")),
          recovery_(
              btrfs,
              pending_markers_,
              std::make_unique<btrfsbackup::platform::linux::SafeDirectoryRoot>("/"),
              std::make_unique<btrfsbackup::platform::linux::SafeDirectoryRoot>(target_root)
          ),
          retention_(
              btrfs,
              std::make_unique<btrfsbackup::platform::linux::SafeDirectoryRoot>("/"),
              std::make_unique<btrfsbackup::platform::linux::SafeDirectoryRoot>(target_root)
          ),
          hook_executables_(std::make_unique<test_support::FakeTrustedExecutableResolver>()),
          hooks_(commands, *hook_executables_),
          local_repository_root_(std::make_unique<btrfsbackup::platform::linux::SafeDirectoryRoot>("/")),
          target_repository_root_(std::make_unique<btrfsbackup::platform::linux::SafeDirectoryRoot>(target_root)),
          repository_(
              btrfs,
              pending_markers_,
              *local_repository_root_,
              *target_repository_root_
          ),
          dispatcher_(snapshots_, recovery_, retention_, hooks_, repository_) {
    }

    ActionHandlerFixture(
        FakeBtrfsOperations& btrfs,
        FakeFileSystem& filesystem,
        FakeCommandRunner& commands,
        const fs::path& target_root,
        const fs::path& hook_root,
        const btrfsbackup::backup::TrustedExecutablePolicy& hook_policy
    )
        : pending_markers_(durable_files_),
          snapshots_(btrfs, filesystem, pending_markers_, std::make_unique<btrfsbackup::platform::linux::SafeDirectoryRoot>("/")),
          recovery_(
              btrfs,
              pending_markers_,
              std::make_unique<btrfsbackup::platform::linux::SafeDirectoryRoot>("/"),
              std::make_unique<btrfsbackup::platform::linux::SafeDirectoryRoot>(target_root)
          ),
          retention_(
              btrfs,
              std::make_unique<btrfsbackup::platform::linux::SafeDirectoryRoot>("/"),
              std::make_unique<btrfsbackup::platform::linux::SafeDirectoryRoot>(target_root)
          ),
          hook_executables_(std::make_unique<btrfsbackup::platform::linux::PosixTrustedExecutableResolver>(hook_root, hook_policy)),
          hooks_(commands, *hook_executables_),
          local_repository_root_(std::make_unique<btrfsbackup::platform::linux::SafeDirectoryRoot>("/")),
          target_repository_root_(std::make_unique<btrfsbackup::platform::linux::SafeDirectoryRoot>(target_root)),
          repository_(
              btrfs,
              pending_markers_,
              *local_repository_root_,
              *target_repository_root_
          ),
          dispatcher_(snapshots_, recovery_, retention_, hooks_, repository_) {
    }

    void handle(
        const btrfsbackup::backup::BackupRunAction& action,
        const btrfsbackup::backup::BackupRunPlan& plan,
        btrfsbackup::CancellationToken& cancellation
    ) override {
        dispatcher_.handle(action, plan, cancellation);
    }

  private:
    FakeCommandRunner fallback_commands_;
    btrfsbackup::platform::linux::PosixDurableFileOperations durable_files_;
    btrfsbackup::state::FilePendingMarkerStore pending_markers_;
    btrfsbackup::backup::SnapshotActionHandler snapshots_;
    btrfsbackup::backup::RecoveryActionHandler recovery_;
    btrfsbackup::backup::RetentionActionHandler retention_;
    std::unique_ptr<btrfsbackup::backup::ITrustedExecutableResolver> hook_executables_;
    btrfsbackup::backup::HookActionHandler hooks_;
    std::unique_ptr<btrfsbackup::backup::ISafeDirectoryRoot> local_repository_root_;
    std::unique_ptr<btrfsbackup::backup::ISafeDirectoryRoot> target_repository_root_;
    btrfsbackup::backup::RepositoryActionHandler repository_;
    btrfsbackup::backup::BackupRunActionHandler dispatcher_;
};

btrfsbackup::backup::BackupRunPlan run_plan() {
    return btrfsbackup::backup::BackupRunPlan{
        .profile_id = btrfsbackup::ProfileId{"default"},
        .run_id = btrfsbackup::RunId{"20260823T120000Z-123-456"},
    };
}

btrfsbackup::backup::BackupSourceRunPlan source_plan(const fs::path& root) {
    btrfsbackup::backup::BackupSourceRunPlan source{
        .source_id = btrfsbackup::SourceId{"root"},
        .local_retention = {.source_id = btrfsbackup::SourceId{"root"}},
        .remote_retention = {.source_id = btrfsbackup::SourceId{"root"}},
    };
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

btrfsbackup::backup::BackupRunAction action(
    btrfsbackup::backup::BackupRunActionKind kind,
    const btrfsbackup::backup::BackupSourceRunPlan& source
) {
    switch (kind) {
    case btrfsbackup::backup::BackupRunActionKind::RecoverPending:
        return btrfsbackup::backup::RecoverPendingAction{source.source_id, source.recovery};
    case btrfsbackup::backup::BackupRunActionKind::CleanupIncoming:
        return btrfsbackup::backup::CleanupIncomingAction{source.source_id, source.incoming_source_root};
    case btrfsbackup::backup::BackupRunActionKind::CreateSnapshot:
        return btrfsbackup::backup::CreateSnapshotAction{
            source.source_id,
            source.source_subvolume,
            source.local_snapshot_dir,
            source.local_snapshot_path,
            source.final_remote_snapshot_path,
            source.recovery.marker_path.parent_path(),
            run_plan().run_id,
        };
    case btrfsbackup::backup::BackupRunActionKind::SendReceive:
        return btrfsbackup::backup::SendReceiveAction{
            source.source_id,
            source.local_snapshot_path,
            std::nullopt,
            source.remote_snapshot_dir,
            source.incoming_run_dir,
        };
    case btrfsbackup::backup::BackupRunActionKind::VerifyReceived:
        return btrfsbackup::backup::VerifyReceivedAction{
            source.source_id,
            source.local_snapshot_path,
            source.received_snapshot_path,
        };
    case btrfsbackup::backup::BackupRunActionKind::CommitReceived:
        return btrfsbackup::backup::CommitReceivedAction{
            source.source_id,
            source.local_snapshot_path,
            source.received_snapshot_path,
            source.final_remote_snapshot_path,
        };
    case btrfsbackup::backup::BackupRunActionKind::ApplyRemoteRetention:
        return btrfsbackup::backup::ApplyRemoteRetentionAction{source.source_id, source.remote_retention};
    case btrfsbackup::backup::BackupRunActionKind::ApplyLocalRetention:
        return btrfsbackup::backup::ApplyLocalRetentionAction{source.source_id, source.local_retention};
    case btrfsbackup::backup::BackupRunActionKind::CleanupSource:
        return btrfsbackup::backup::CleanupSourceAction{
            source.source_id,
            source.received_snapshot_path,
            source.incoming_run_dir,
            source.recovery.marker_path,
            source.recovery.marker_path.parent_path(),
        };
    case btrfsbackup::backup::BackupRunActionKind::BeforeSnapshotHook:
    case btrfsbackup::backup::BackupRunActionKind::AfterSnapshotHook:
        break;
    }
    throw std::logic_error("hook action requires hook_action");
}

btrfsbackup::backup::BackupRunAction hook_action(btrfsbackup::backup::HookPhase phase) {
    return btrfsbackup::backup::RunHookAction{
        btrfsbackup::SourceId{"root"},
        phase,
        btrfsbackup::config::ProfileHookCommand{
            .program = "/etc/btrfs-backup/hooks.d/prepare-backup",
            .arguments = {"--source", "root"},
            .timeout = std::chrono::seconds{300},
        },
    };
}

void handle_action(
    btrfsbackup::backup::IBackupRunActionHandler& handler,
    const btrfsbackup::backup::BackupRunAction& action
) {
    btrfsbackup::CancellationToken cancellation;
    handler.handle(action, run_plan(), cancellation);
}

void test_create_snapshot_writes_pending_marker_and_verifies_readonly_snapshot() {
    fs::path root = test_helpers::test_root("backup-run-action-handler", "create-snapshot");
    btrfsbackup::backup::BackupSourceRunPlan source = source_plan(root);
    FakeBtrfsOperations btrfs;
    btrfs.metadata_by_path[source.local_snapshot_path.string()] = btrfsbackup::backup::SnapshotMetadata{
        .is_subvolume = true,
        .readonly = true,
        .uuid = "local-uuid",
    };
    FakeFileSystem fs_effects;
    ActionHandlerFixture handler(btrfs, fs_effects);

    handle_action(handler, action(btrfsbackup::backup::BackupRunActionKind::CreateSnapshot, source));

    test_helpers::expect_eq("mkdir local snapshot dir", fs_effects.calls.at(0), action_path("mkdir", source.local_snapshot_dir));
    test_helpers::expect_eq("snapshot call", btrfs.calls.at(0), "snapshot:" + source.source_subvolume.string() + "->" + source.local_snapshot_path.string());
    test_helpers::expect_eq("metadata call", btrfs.calls.at(1), action_path("metadata", source.local_snapshot_path));
    test_helpers::expect_true("pending marker exists", fs::is_regular_file(source.recovery.marker_path), "pending marker should be written");
}

void test_cleanup_incoming_uses_safe_root() {
    fs::path root = test_helpers::test_root("backup-run-action-handler", "cleanup-incoming");
    btrfsbackup::backup::BackupSourceRunPlan source = source_plan(root);
    fs::path directory = source.incoming_source_root / "old-dir";
    fs::create_directories(directory);
    test_helpers::write_file(directory / "data", "remove\n");
    FakeBtrfsOperations btrfs;
    FakeFileSystem fs_effects;
    ActionHandlerFixture handler(btrfs, fs_effects);

    handle_action(handler, action(btrfsbackup::backup::BackupRunActionKind::CleanupIncoming, source));

    test_helpers::expect_true("incoming root preserved", fs::is_directory(source.incoming_source_root), "cleanup should preserve the incoming source root");
    test_helpers::expect_true("incoming contents removed", fs::is_empty(source.incoming_source_root), "cleanup should remove incoming contents through the safe root");
}

void test_production_cleanup_rejects_incoming_symlink_escape() {
    fs::path root = test_helpers::test_root("backup-run-action-handler", "cleanup-symlink");
    fs::path target = root / "target";
    fs::path outside = root / "outside";
    fs::create_directories(target / ".incoming");
    fs::create_directories(outside);
    test_helpers::write_file(outside / "sentinel", "keep\n");
    fs::create_directory_symlink(outside, target / ".incoming" / "root");

    btrfsbackup::backup::BackupSourceRunPlan source = source_plan(root);
    source.incoming_source_root = target / ".incoming" / "root";
    source.incoming_run_dir = source.incoming_source_root / "run-1";
    FakeBtrfsOperations btrfs;
    FakeFileSystem fs_effects;
    FakeCommandRunner hooks;
    ActionHandlerFixture handler(btrfs, fs_effects, hooks, target);

    test_helpers::expect_validation_error("production cleanup symlink", [&] { handle_action(handler, action(btrfsbackup::backup::BackupRunActionKind::CleanupIncoming, source)); }, "Too many levels of symbolic links");
    test_helpers::expect_true(
        "production cleanup outside preserved",
        fs::is_regular_file(outside / "sentinel"),
        "cleanup followed incoming symlink outside target"
    );

    fs::remove_all(root);
}

void test_verify_commit_retention_and_cleanup_use_existing_helpers() {
    fs::path root = test_helpers::test_root("backup-run-action-handler", "commit-cleanup");
    btrfsbackup::backup::BackupSourceRunPlan source = source_plan(root);
    source.local_retention.delete_snapshots = {
        btrfsbackup::backup::SnapshotInfo{.source_id = btrfsbackup::SourceId{"root"}, .path = root / "local" / "old"},
    };
    source.remote_retention.delete_snapshots = {
        btrfsbackup::backup::SnapshotInfo{.source_id = btrfsbackup::SourceId{"root"}, .path = root / "remote" / "old"},
    };

    FakeBtrfsOperations btrfs;
    btrfs.subvolumes = {source.received_snapshot_path.string()};
    btrfs.metadata_by_path[source.local_snapshot_path.string()] = btrfsbackup::backup::SnapshotMetadata{
        .is_subvolume = true,
        .readonly = true,
        .uuid = "local-uuid",
    };
    btrfs.metadata_by_path[source.received_snapshot_path.string()] = btrfsbackup::backup::SnapshotMetadata{
        .is_subvolume = true,
        .readonly = true,
        .received_uuid = "local-uuid",
    };
    btrfs.metadata_by_path[source.final_remote_snapshot_path.string()] = btrfsbackup::backup::SnapshotMetadata{
        .is_subvolume = true,
        .readonly = true,
        .received_uuid = "local-uuid",
    };

    FakeFileSystem fs_effects;
    fs::create_directories(source.received_snapshot_path);
    ActionHandlerFixture handler(btrfs, fs_effects);

    handle_action(handler, action(btrfsbackup::backup::BackupRunActionKind::VerifyReceived, source));
    handle_action(handler, action(btrfsbackup::backup::BackupRunActionKind::CommitReceived, source));
    handle_action(handler, action(btrfsbackup::backup::BackupRunActionKind::ApplyRemoteRetention, source));
    handle_action(handler, action(btrfsbackup::backup::BackupRunActionKind::ApplyLocalRetention, source));
    handle_action(handler, action(btrfsbackup::backup::BackupRunActionKind::CleanupSource, source));

    test_helpers::expect_true("commit snapshot", std::find(btrfs.calls.begin(), btrfs.calls.end(), "snapshot:" + source.received_snapshot_path.string() + "->" + source.final_remote_snapshot_path.string()) != btrfs.calls.end(), "commit should snapshot received subvolume");
    test_helpers::expect_true("remote retention delete", std::find(btrfs.calls.begin(), btrfs.calls.end(), action_path("delete", root / "remote" / "old")) != btrfs.calls.end(), "remote retention should delete planned snapshot");
    test_helpers::expect_true("local retention delete", std::find(btrfs.calls.begin(), btrfs.calls.end(), action_path("delete", root / "local" / "old")) != btrfs.calls.end(), "local retention should delete planned snapshot");
    test_helpers::expect_true("cleanup received", !fs::exists(source.received_snapshot_path), "cleanup should delete the received snapshot through the safe root");
}

void test_pending_recovery_deletes_invalid_remote_snapshot_first() {
    fs::path root = test_helpers::test_root("backup-run-action-handler", "recover-invalid-commit");
    btrfsbackup::backup::BackupSourceRunPlan source = source_plan(root);
    source.recovery.delete_remote_snapshot = true;
    source.recovery.remote_snapshot_path = source.final_remote_snapshot_path;
    source.recovery.delete_local_snapshot = true;
    source.recovery.local_snapshot_path = source.local_snapshot_path;
    FakeBtrfsOperations btrfs;
    FakeFileSystem fs_effects;
    ActionHandlerFixture handler(btrfs, fs_effects);

    handle_action(handler, action(btrfsbackup::backup::BackupRunActionKind::RecoverPending, source));

    test_helpers::expect_eq("recovery delete count", std::to_string(btrfs.calls.size()), "2");
    test_helpers::expect_eq("recovery remote first", btrfs.calls.at(0), action_path("delete", source.final_remote_snapshot_path));
    test_helpers::expect_eq("recovery local second", btrfs.calls.at(1), action_path("delete", source.local_snapshot_path));
}

void test_failed_remote_recovery_keeps_local_snapshot_and_marker() {
    fs::path root = test_helpers::test_root("backup-run-action-handler", "recover-invalid-commit-fails");
    btrfsbackup::backup::BackupSourceRunPlan source = source_plan(root);
    source.recovery.delete_remote_snapshot = true;
    source.recovery.remote_snapshot_path = source.final_remote_snapshot_path;
    source.recovery.delete_local_snapshot = true;
    source.recovery.local_snapshot_path = source.local_snapshot_path;
    source.recovery.clear_marker = true;
    btrfsbackup::platform::linux::PosixDurableFileOperations durable_files;
    btrfsbackup::state::write_pending_marker(
        durable_files,
        root / "state",
        btrfsbackup::backup::PendingMarker{
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

    test_helpers::expect_validation_error("failed recovery delete", [&] { handle_action(handler, action(btrfsbackup::backup::BackupRunActionKind::RecoverPending, source)); }, "injected subvolume delete failure");

    test_helpers::expect_eq("failed recovery delete count", std::to_string(btrfs.calls.size()), "1");
    test_helpers::expect_eq("failed recovery stops at remote", btrfs.calls.at(0), action_path("delete", source.final_remote_snapshot_path));
    test_helpers::expect_true("failed recovery keeps marker", fs::is_regular_file(source.recovery.marker_path), "pending marker must remain after failed cleanup");
    fs::remove_all(root);
}

void test_hook_actions_use_command_runner_argv() {
    fs::path root = test_helpers::test_root("backup-run-action-handler", "hooks");
    btrfsbackup::backup::BackupSourceRunPlan source = source_plan(root);
    FakeBtrfsOperations btrfs;
    FakeFileSystem fs_effects;
    FakeCommandRunner hooks;
    ActionHandlerFixture handler(btrfs, fs_effects, hooks);

    handle_action(handler, hook_action(btrfsbackup::backup::HookPhase::BeforeSnapshot));

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

    btrfsbackup::backup::BackupSourceRunPlan source = source_plan(root);
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
    btrfsbackup::backup::BackupRunAction trusted_hook = hook_action(btrfsbackup::backup::HookPhase::BeforeSnapshot);
    std::get<btrfsbackup::backup::RunHookAction>(trusted_hook).hook.program = program.string();

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
    btrfsbackup::backup::BackupSourceRunPlan source = source_plan(root);
    FakeBtrfsOperations btrfs;
    FakeFileSystem fs_effects;
    FakeCommandRunner hooks;
    hooks.exit_code = 42;
    ActionHandlerFixture handler(btrfs, fs_effects, hooks);

    try {
        handle_action(handler, hook_action(btrfsbackup::backup::HookPhase::AfterSnapshot));
        test_helpers::expect_true("hook failure type", false, "hook failure should fail the action");
    } catch (const btrfsbackup::SystemOperationError& error) {
        test_helpers::expect_contains("hook failure message", error.what(), "hook failed");
    }
}

void test_hook_timeout_has_stable_error_code() {
    fs::path root = test_helpers::test_root("backup-run-action-handler", "hook-timeout");
    btrfsbackup::backup::BackupSourceRunPlan source = source_plan(root);
    FakeBtrfsOperations btrfs;
    FakeFileSystem fs_effects;
    FakeCommandRunner hooks;
    hooks.timed_out = true;
    ActionHandlerFixture handler(btrfs, fs_effects, hooks);
    btrfsbackup::backup::BackupRunAction timed_hook = hook_action(btrfsbackup::backup::HookPhase::BeforeSnapshot);
    std::get<btrfsbackup::backup::RunHookAction>(timed_hook).hook.timeout = std::chrono::seconds{17};

    try {
        handle_action(handler, timed_hook);
        test_helpers::expect_true("hook timeout throws", false, "timeout should fail the action");
    } catch (const btrfsbackup::CodedOperationError& error) {
        test_helpers::expect_true(
            "hook timeout code",
            error.error_code == btrfsbackup::ErrorCode::HookBeforeSnapshotTimeout,
            "unexpected error code"
        );
        test_helpers::expect_contains("hook timeout message", error.what(), "17 seconds");
    }
}

void test_hook_cancellation_is_not_reported_as_hook_failure() {
    fs::path root = test_helpers::test_root("backup-run-action-handler", "hook-cancel");
    btrfsbackup::backup::BackupSourceRunPlan source = source_plan(root);
    FakeBtrfsOperations btrfs;
    FakeFileSystem fs_effects;
    FakeCommandRunner hooks;
    hooks.cancelled = true;
    ActionHandlerFixture handler(btrfs, fs_effects, hooks);

    bool cancelled = false;
    try {
        handle_action(handler, hook_action(btrfsbackup::backup::HookPhase::AfterSnapshot));
    } catch (const btrfsbackup::OperationCancelledError&) {
        cancelled = true;
    }
    test_helpers::expect_true("hook cancellation type", cancelled, "hook cancellation should have a distinct result");
}

void test_default_factory_builds_run_scoped_dispatcher() {
    FakeBtrfsOperations btrfs;
    FakeFileSystem filesystem;
    FakeCommandRunner commands;
    btrfsbackup::platform::linux::PosixDurableFileOperations durable_files;
    btrfsbackup::state::FilePendingMarkerStore pending_markers(durable_files);
    test_support::FakeSafeDirectoryRootFactory safe_directories;
    test_support::FakeTrustedExecutableResolver hook_executables;
    btrfsbackup::backup::DefaultBackupRunActionHandlerFactory factory(
        btrfs,
        filesystem,
        commands,
        pending_markers,
        safe_directories,
        hook_executables
    );
    btrfsbackup::backup::BackupRunPlan plan = run_plan();
    plan.target_mount_point = "/mnt/backup/default";
    std::unique_ptr<btrfsbackup::backup::IBackupRunActionHandler> handler = factory.create(plan);
    btrfsbackup::CancellationToken cancellation;

    handler->handle(
        hook_action(btrfsbackup::backup::HookPhase::BeforeSnapshot),
        plan,
        cancellation
    );

    test_helpers::expect_true(
        "default factory hook",
        commands.calls.size() == 1,
        "run-scoped dispatcher did not route the hook action"
    );
}

} // namespace

int main() {
    test_create_snapshot_writes_pending_marker_and_verifies_readonly_snapshot();
    test_cleanup_incoming_uses_safe_root();
    test_production_cleanup_rejects_incoming_symlink_escape();
    test_verify_commit_retention_and_cleanup_use_existing_helpers();
    test_pending_recovery_deletes_invalid_remote_snapshot_first();
    test_failed_remote_recovery_keeps_local_snapshot_and_marker();
    test_hook_actions_use_command_runner_argv();
    test_production_hook_uses_pinned_trusted_descriptor();
    test_hook_failure_is_reported_as_system_operation_error();
    test_hook_timeout_has_stable_error_code();
    test_hook_cancellation_is_not_reported_as_hook_failure();
    test_default_factory_builds_run_scoped_dispatcher();

    return test_helpers::finish("backup run action handler tests");
}
