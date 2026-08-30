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

#include <backup/execution/actions/BackupRunActionHandler.hpp>
#include <backup/execution/actions/HookActionHandler.hpp>
#include <backup/ports/IBtrfsOperations.hpp>
#include <backup/ports/ICommandRunner.hpp>
#include <backup/ports/IFileSystem.hpp>
#include <backup/ports/RunContext.hpp>
#include <backup/execution/actions/RecoveryActionHandler.hpp>
#include <backup/execution/actions/RepositoryActionHandler.hpp>
#include <backup/execution/actions/RetentionActionHandler.hpp>
#include <backup/execution/actions/SnapshotActionHandler.hpp>
#include <backup/execution/actions/DefaultBackupRunActionHandlerFactory.hpp>
#include <backup/execution/SystemRunContext.hpp>

#include <platform/linux/filesystem/SafeDirectoryRoot.hpp>
#include <platform/linux/filesystem/PosixDurableFileOperations.hpp>
#include <platform/linux/filesystem/TrustedExecutable.hpp>
#include <state/persistence/FilePendingMarkerStore.hpp>
#include <state/query/RunState.hpp>

#include "support/FakeTrustedExecutable.hpp"
#include "support/FakeSafeDirectory.hpp"
#include "support/ValidationTestHelpers.hpp"

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

static_assert(HandlesAction<btrfsbackup::backup::execution::SnapshotActionHandler, btrfsbackup::backup::CreateSnapshotAction>);
static_assert(!HandlesAction<btrfsbackup::backup::execution::SnapshotActionHandler, btrfsbackup::backup::RecoverPendingAction>);
static_assert(HandlesAction<btrfsbackup::backup::execution::RecoveryActionHandler, btrfsbackup::backup::RecoverPendingAction>);
static_assert(!HandlesAction<btrfsbackup::backup::execution::RecoveryActionHandler, btrfsbackup::backup::CreateSnapshotAction>);
static_assert(HandlesAction<btrfsbackup::backup::execution::RetentionActionHandler, btrfsbackup::backup::ApplyRemoteRetentionAction>);
static_assert(HandlesAction<btrfsbackup::backup::execution::RetentionActionHandler, btrfsbackup::backup::ApplyLocalRetentionAction>);
static_assert(!HandlesAction<btrfsbackup::backup::execution::RetentionActionHandler, btrfsbackup::backup::CleanupSourceAction>);
static_assert(HandlesAction<btrfsbackup::backup::execution::RepositoryActionHandler, btrfsbackup::backup::CleanupIncomingAction>);
static_assert(HandlesAction<btrfsbackup::backup::execution::RepositoryActionHandler, btrfsbackup::backup::VerifyReceivedAction>);
static_assert(HandlesAction<btrfsbackup::backup::execution::RepositoryActionHandler, btrfsbackup::backup::CommitReceivedAction>);
static_assert(HandlesAction<btrfsbackup::backup::execution::RepositoryActionHandler, btrfsbackup::backup::CleanupSourceAction>);
static_assert(!HandlesAction<btrfsbackup::backup::execution::RepositoryActionHandler, btrfsbackup::backup::SendReceiveAction>);
static_assert(!std::is_constructible_v<
              btrfsbackup::backup::execution::RepositoryActionHandler,
              btrfsbackup::backup::IBtrfsOperations&,
              btrfsbackup::backup::IFileSystem&,
              btrfsbackup::backup::IPendingMarkerStore&>);
static_assert(std::is_constructible_v<
              btrfsbackup::backup::execution::RepositoryActionHandler,
              btrfsbackup::backup::IBtrfsOperations&,
              btrfsbackup::backup::IPendingMarkerStore&,
              btrfsbackup::backup::ISafeDirectoryRoot&,
              btrfsbackup::backup::ISafeDirectoryRoot&>);
static_assert(HandlesHookAction<btrfsbackup::backup::execution::HookActionHandler>);
static_assert(!HandlesAction<btrfsbackup::backup::execution::HookActionHandler, btrfsbackup::backup::RunHookAction>);

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

class ActionHandlerFixture final : public btrfsbackup::backup::execution::IBackupRunActionHandler {
  public:
    ActionHandlerFixture(FakeBtrfsOperations& btrfs, FakeFileSystem& filesystem)
        : pending_markers_(durable_files_),
          snapshots_(btrfs, filesystem, pending_markers_, clock_),
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
          snapshots_(btrfs, filesystem, pending_markers_, clock_),
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
          snapshots_(btrfs, filesystem, pending_markers_, clock_, std::make_unique<btrfsbackup::platform::linux::filesystem::SafeDirectoryRoot>("/")),
          recovery_(
              btrfs,
              pending_markers_,
              std::make_unique<btrfsbackup::platform::linux::filesystem::SafeDirectoryRoot>("/"),
              std::make_unique<btrfsbackup::platform::linux::filesystem::SafeDirectoryRoot>(target_root)
          ),
          retention_(
              btrfs,
              std::make_unique<btrfsbackup::platform::linux::filesystem::SafeDirectoryRoot>("/"),
              std::make_unique<btrfsbackup::platform::linux::filesystem::SafeDirectoryRoot>(target_root)
          ),
          hook_executables_(std::make_unique<test_support::FakeTrustedExecutableResolver>()),
          hooks_(commands, *hook_executables_),
          local_repository_root_(std::make_unique<btrfsbackup::platform::linux::filesystem::SafeDirectoryRoot>("/")),
          target_repository_root_(std::make_unique<btrfsbackup::platform::linux::filesystem::SafeDirectoryRoot>(target_root)),
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
          snapshots_(btrfs, filesystem, pending_markers_, clock_, std::make_unique<btrfsbackup::platform::linux::filesystem::SafeDirectoryRoot>("/")),
          recovery_(
              btrfs,
              pending_markers_,
              std::make_unique<btrfsbackup::platform::linux::filesystem::SafeDirectoryRoot>("/"),
              std::make_unique<btrfsbackup::platform::linux::filesystem::SafeDirectoryRoot>(target_root)
          ),
          retention_(
              btrfs,
              std::make_unique<btrfsbackup::platform::linux::filesystem::SafeDirectoryRoot>("/"),
              std::make_unique<btrfsbackup::platform::linux::filesystem::SafeDirectoryRoot>(target_root)
          ),
          hook_executables_(std::make_unique<btrfsbackup::platform::linux::filesystem::PosixTrustedExecutableResolver>(hook_root, hook_policy)),
          hooks_(commands, *hook_executables_),
          local_repository_root_(std::make_unique<btrfsbackup::platform::linux::filesystem::SafeDirectoryRoot>("/")),
          target_repository_root_(std::make_unique<btrfsbackup::platform::linux::filesystem::SafeDirectoryRoot>(target_root)),
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
    class FixedClock final : public btrfsbackup::backup::IClock {
      public:
        btrfsbackup::RuntimeTimePoint now() const override {
            return test_helpers::runtime_time("2026-08-23T12:00:00Z");
        }

        btrfsbackup::LocalDate local_date() const override {
            return *btrfsbackup::parse_local_date("2026-08-23");
        }
    };

    FakeCommandRunner fallback_commands_;
    btrfsbackup::platform::linux::filesystem::PosixDurableFileOperations durable_files_;
    btrfsbackup::state::FilePendingMarkerStore pending_markers_;
    FixedClock clock_;
    btrfsbackup::backup::execution::SnapshotActionHandler snapshots_;
    btrfsbackup::backup::execution::RecoveryActionHandler recovery_;
    btrfsbackup::backup::execution::RetentionActionHandler retention_;
    std::unique_ptr<btrfsbackup::backup::ITrustedExecutableResolver> hook_executables_;
    btrfsbackup::backup::execution::HookActionHandler hooks_;
    std::unique_ptr<btrfsbackup::backup::ISafeDirectoryRoot> local_repository_root_;
    std::unique_ptr<btrfsbackup::backup::ISafeDirectoryRoot> target_repository_root_;
    btrfsbackup::backup::execution::RepositoryActionHandler repository_;
    btrfsbackup::backup::execution::BackupRunActionHandler dispatcher_;
};

btrfsbackup::backup::BackupRunPlan run_plan() {
    return btrfsbackup::backup::BackupRunPlan{
        .profile_id = btrfsbackup::ProfileId{"default"},
        .run_id = btrfsbackup::RunId{"20260823T120000Z-123-456"},
    };
}

struct TestSource {
    btrfsbackup::SourceId source_id{"root"};
    fs::path source_subvolume;
    fs::path local_snapshot_dir;
    fs::path remote_snapshot_dir;
    fs::path incoming_source_root;
    fs::path incoming_run_dir;
    fs::path local_snapshot_path;
    fs::path received_snapshot_path;
    fs::path final_remote_snapshot_path;
};

TestSource source_plan(const fs::path& root) {
    TestSource source;
    source.source_subvolume = root / "source";
    source.local_snapshot_dir = btrfsbackup::config::LocalSnapshotRoot{root / "local"};
    source.remote_snapshot_dir = root / "remote";
    source.incoming_source_root = root / "incoming" / "root";
    source.incoming_run_dir = source.incoming_source_root / "run-1";
    source.local_snapshot_path = source.local_snapshot_dir / "root-2026-08-23T120000Z";
    source.received_snapshot_path = source.incoming_run_dir / "root-2026-08-23T120000Z";
    source.final_remote_snapshot_path = source.remote_snapshot_dir / "root-2026-08-23T120000Z";
    return source;
}

fs::path marker_path(const TestSource& source) {
    return source.local_snapshot_dir.parent_path() / "state" / "pending-root";
}

btrfsbackup::backup::BackupRunAction action(
    btrfsbackup::backup::BackupRunActionKind kind,
    const TestSource& source,
    const btrfsbackup::backup::PendingRecoveryPlan& recovery,
    const btrfsbackup::backup::RetentionPlan& local_retention,
    const btrfsbackup::backup::RetentionPlan& remote_retention
) {
    switch (kind) {
    case btrfsbackup::backup::BackupRunActionKind::RecoverPending:
        return btrfsbackup::backup::RecoverPendingAction{source.source_id, recovery};
    case btrfsbackup::backup::BackupRunActionKind::CleanupIncoming:
        return btrfsbackup::backup::CleanupIncomingAction{source.source_id, source.incoming_source_root};
    case btrfsbackup::backup::BackupRunActionKind::CreateSnapshot:
        return btrfsbackup::backup::CreateSnapshotAction{
            source.source_id,
            source.source_subvolume,
            source.local_snapshot_dir,
            source.local_snapshot_path,
            source.final_remote_snapshot_path,
            recovery.marker_path.parent_path(),
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
        return btrfsbackup::backup::ApplyRemoteRetentionAction{source.source_id, remote_retention};
    case btrfsbackup::backup::BackupRunActionKind::ApplyLocalRetention:
        return btrfsbackup::backup::ApplyLocalRetentionAction{source.source_id, local_retention};
    case btrfsbackup::backup::BackupRunActionKind::CleanupSource:
        return btrfsbackup::backup::CleanupSourceAction{
            source.source_id,
            source.received_snapshot_path,
            source.incoming_run_dir,
            recovery.marker_path,
            recovery.marker_path.parent_path(),
        };
    case btrfsbackup::backup::BackupRunActionKind::BeforeSnapshotHook:
    case btrfsbackup::backup::BackupRunActionKind::AfterSnapshotHook:
        break;
    }
    throw std::logic_error("hook action requires hook_action");
}

btrfsbackup::backup::BackupRunAction action(
    btrfsbackup::backup::BackupRunActionKind kind,
    const TestSource& source
) {
    btrfsbackup::backup::PendingRecoveryPlan recovery;
    recovery.marker_path = marker_path(source);
    return action(
        kind,
        source,
        recovery,
        btrfsbackup::backup::RetentionPlan{.source_id = source.source_id},
        btrfsbackup::backup::RetentionPlan{.source_id = source.source_id}
    );
}

btrfsbackup::backup::BackupRunAction hook_action(btrfsbackup::backup::HookPhase phase) {
    return btrfsbackup::backup::RunHookAction{
        btrfsbackup::SourceId{"root"},
        phase,
        btrfsbackup::config::ProfileHookCommand{
            .program = btrfsbackup::config::HookProgramPath{"/etc/btrfs-backup/hooks.d/prepare-backup"},
            .arguments = {"--source", "root"},
            .timeout = std::chrono::seconds{300},
        },
    };
}

void handle_action(
    btrfsbackup::backup::execution::IBackupRunActionHandler& handler,
    const btrfsbackup::backup::BackupRunAction& action
) {
    btrfsbackup::CancellationToken cancellation;
    handler.handle(action, run_plan(), cancellation);
}

void test_create_snapshot_writes_pending_marker_and_verifies_readonly_snapshot() {
    fs::path root = test_helpers::test_root("backup-run-action-handler", "create-snapshot");
    TestSource source = source_plan(root);
    FakeBtrfsOperations btrfs;
    btrfs.metadata_by_path[source.local_snapshot_path.string()] = btrfsbackup::backup::SnapshotMetadata{
        .is_subvolume = true,
        .readonly = true,
        .uuid = btrfsbackup::backup::SnapshotUuid{"local-uuid"},
    };
    FakeFileSystem fs_effects;
    ActionHandlerFixture handler(btrfs, fs_effects);

    handle_action(handler, action(btrfsbackup::backup::BackupRunActionKind::CreateSnapshot, source));

    test_helpers::expect_eq("mkdir local snapshot dir", fs_effects.calls.at(0), action_path("mkdir", source.local_snapshot_dir));
    test_helpers::expect_eq("snapshot call", btrfs.calls.at(0), "snapshot:" + source.source_subvolume.string() + "->" + source.local_snapshot_path.string());
    test_helpers::expect_eq("metadata call", btrfs.calls.at(1), action_path("metadata", source.local_snapshot_path));
    test_helpers::expect_true("pending marker exists", fs::is_regular_file(marker_path(source)), "pending marker should be written");
    btrfsbackup::platform::linux::filesystem::PosixDurableFileOperations durable_files;
    btrfsbackup::state::FilePendingMarkerStore pending_markers(durable_files);
    const auto marker = pending_markers.read(root / "state", source.source_id);
    test_helpers::expect_true(
        "pending marker clock",
        marker.has_value() && marker->timestamp == test_helpers::runtime_time("2026-08-23T12:00:00Z"),
        "pending marker should use the injected clock"
    );
}

void test_cleanup_incoming_uses_safe_root() {
    fs::path root = test_helpers::test_root("backup-run-action-handler", "cleanup-incoming");
    TestSource source = source_plan(root);
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

    TestSource source = source_plan(root);
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
    TestSource source = source_plan(root);
    btrfsbackup::backup::RetentionPlan local_retention{.source_id = source.source_id};
    local_retention.delete_snapshots = {
        btrfsbackup::backup::SnapshotInfo{.source_id = btrfsbackup::SourceId{"root"}, .path = root / "local" / "old"},
    };
    btrfsbackup::backup::RetentionPlan remote_retention{.source_id = source.source_id};
    remote_retention.delete_snapshots = {
        btrfsbackup::backup::SnapshotInfo{.source_id = btrfsbackup::SourceId{"root"}, .path = root / "remote" / "old"},
    };
    btrfsbackup::backup::PendingRecoveryPlan recovery;
    recovery.marker_path = marker_path(source);

    FakeBtrfsOperations btrfs;
    btrfs.subvolumes = {source.received_snapshot_path.string()};
    btrfs.metadata_by_path[source.local_snapshot_path.string()] = btrfsbackup::backup::SnapshotMetadata{
        .is_subvolume = true,
        .readonly = true,
        .uuid = btrfsbackup::backup::SnapshotUuid{"local-uuid"},
    };
    btrfs.metadata_by_path[source.received_snapshot_path.string()] = btrfsbackup::backup::SnapshotMetadata{
        .is_subvolume = true,
        .readonly = true,
        .received_uuid = btrfsbackup::backup::ReceivedSnapshotUuid{"local-uuid"},
    };
    btrfs.metadata_by_path[source.final_remote_snapshot_path.string()] = btrfsbackup::backup::SnapshotMetadata{
        .is_subvolume = true,
        .readonly = true,
        .received_uuid = btrfsbackup::backup::ReceivedSnapshotUuid{"local-uuid"},
    };

    FakeFileSystem fs_effects;
    fs::create_directories(source.received_snapshot_path);
    ActionHandlerFixture handler(btrfs, fs_effects);

    handle_action(handler, action(btrfsbackup::backup::BackupRunActionKind::VerifyReceived, source));
    handle_action(handler, action(btrfsbackup::backup::BackupRunActionKind::CommitReceived, source));
    handle_action(handler, action(btrfsbackup::backup::BackupRunActionKind::ApplyRemoteRetention, source, recovery, local_retention, remote_retention));
    handle_action(handler, action(btrfsbackup::backup::BackupRunActionKind::ApplyLocalRetention, source, recovery, local_retention, remote_retention));
    handle_action(handler, action(btrfsbackup::backup::BackupRunActionKind::CleanupSource, source));

    test_helpers::expect_true("commit snapshot", std::find(btrfs.calls.begin(), btrfs.calls.end(), "snapshot:" + source.received_snapshot_path.string() + "->" + source.final_remote_snapshot_path.string()) != btrfs.calls.end(), "commit should snapshot received subvolume");
    test_helpers::expect_true("remote retention delete", std::find(btrfs.calls.begin(), btrfs.calls.end(), action_path("delete", root / "remote" / "old")) != btrfs.calls.end(), "remote retention should delete planned snapshot");
    test_helpers::expect_true("local retention delete", std::find(btrfs.calls.begin(), btrfs.calls.end(), action_path("delete", root / "local" / "old")) != btrfs.calls.end(), "local retention should delete planned snapshot");
    test_helpers::expect_true("cleanup received", !fs::exists(source.received_snapshot_path), "cleanup should delete the received snapshot through the safe root");
}

void test_pending_recovery_deletes_invalid_remote_snapshot_first() {
    fs::path root = test_helpers::test_root("backup-run-action-handler", "recover-invalid-commit");
    TestSource source = source_plan(root);
    btrfsbackup::backup::PendingRecoveryPlan recovery;
    recovery.marker_path = marker_path(source);
    recovery.effects = {
        btrfsbackup::backup::DeletePendingRemoteSnapshot{source.final_remote_snapshot_path},
        btrfsbackup::backup::DeletePendingLocalSnapshot{source.local_snapshot_path},
    };
    FakeBtrfsOperations btrfs;
    FakeFileSystem fs_effects;
    ActionHandlerFixture handler(btrfs, fs_effects);

    handle_action(handler, action(btrfsbackup::backup::BackupRunActionKind::RecoverPending, source, recovery, {.source_id = source.source_id}, {.source_id = source.source_id}));

    test_helpers::expect_eq("recovery delete count", std::to_string(btrfs.calls.size()), "2");
    test_helpers::expect_eq("recovery remote first", btrfs.calls.at(0), action_path("delete", source.final_remote_snapshot_path));
    test_helpers::expect_eq("recovery local second", btrfs.calls.at(1), action_path("delete", source.local_snapshot_path));
}

void test_failed_remote_recovery_keeps_local_snapshot_and_marker() {
    fs::path root = test_helpers::test_root("backup-run-action-handler", "recover-invalid-commit-fails");
    TestSource source = source_plan(root);
    btrfsbackup::backup::PendingRecoveryPlan recovery;
    recovery.marker_path = marker_path(source);
    recovery.effects = {
        btrfsbackup::backup::DeletePendingRemoteSnapshot{source.final_remote_snapshot_path},
        btrfsbackup::backup::DeletePendingLocalSnapshot{source.local_snapshot_path},
        btrfsbackup::backup::ClearPendingMarker{recovery.marker_path},
    };
    btrfsbackup::platform::linux::filesystem::PosixDurableFileOperations durable_files;
    btrfsbackup::state::write_pending_marker(
        durable_files,
        root / "state",
        btrfsbackup::backup::PendingMarker{
            .source_id = source.source_id,
            .local_snapshot_path = source.local_snapshot_path,
            .final_snapshot_path = source.final_remote_snapshot_path,
            .run_id = btrfsbackup::RunId{"run-1"},
            .timestamp = test_helpers::runtime_time("2026-08-23T12:00:00Z"),
        }
    );

    FakeBtrfsOperations btrfs;
    btrfs.delete_failure_path = source.final_remote_snapshot_path;
    FakeFileSystem fs_effects;
    ActionHandlerFixture handler(btrfs, fs_effects);

    test_helpers::expect_validation_error("failed recovery delete", [&] { handle_action(handler, action(btrfsbackup::backup::BackupRunActionKind::RecoverPending, source, recovery, {.source_id = source.source_id}, {.source_id = source.source_id})); }, "injected subvolume delete failure");

    test_helpers::expect_eq("failed recovery delete count", std::to_string(btrfs.calls.size()), "1");
    test_helpers::expect_eq("failed recovery stops at remote", btrfs.calls.at(0), action_path("delete", source.final_remote_snapshot_path));
    test_helpers::expect_true("failed recovery keeps marker", fs::is_regular_file(recovery.marker_path), "pending marker must remain after failed cleanup");
    fs::remove_all(root);
}

void test_hook_actions_use_command_runner_argv() {
    fs::path root = test_helpers::test_root("backup-run-action-handler", "hooks");
    TestSource source = source_plan(root);
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
    test_helpers::expect_true(
        "hook environment profile",
        hooks.controlled_options->environment_profile ==
            btrfsbackup::backup::CommandEnvironmentProfile::Hook,
        "hook did not select its explicit environment profile"
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

    TestSource source = source_plan(root);
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
    std::get<btrfsbackup::backup::RunHookAction>(trusted_hook).hook.program =
        btrfsbackup::config::HookProgramPath{program};

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
    TestSource source = source_plan(root);
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
    TestSource source = source_plan(root);
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
    TestSource source = source_plan(root);
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
    btrfsbackup::platform::linux::filesystem::PosixDurableFileOperations durable_files;
    btrfsbackup::state::FilePendingMarkerStore pending_markers(durable_files);
    test_support::FakeSafeDirectoryRootFactory safe_directories;
    test_support::FakeTrustedExecutableResolver hook_executables;
    btrfsbackup::backup::execution::SystemClock clock;
    btrfsbackup::backup::execution::DefaultBackupRunActionHandlerFactory factory(
        btrfs,
        filesystem,
        commands,
        pending_markers,
        clock,
        safe_directories,
        hook_executables
    );
    btrfsbackup::backup::BackupRunPlan plan = run_plan();
    plan.target_mount_point = "/mnt/backup/default";
    std::unique_ptr<btrfsbackup::backup::execution::IBackupRunActionHandler> handler = factory.create(plan);
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
