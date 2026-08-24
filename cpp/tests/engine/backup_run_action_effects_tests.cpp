#include <fcntl.h>
#include <sys/stat.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <btrfsbackup/engine/backup_run_action_effects.hpp>

#include "support/test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

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
        inherited_fds_were_open = !options.inherited_fds.empty()
            && std::all_of(options.inherited_fds.begin(), options.inherited_fds.end(), [](int fd) {
                return fcntl(fd, F_GETFD) >= 0;
            });
        btrfsbackup::CommandResult result = run(argv);
        result.cancelled = cancelled;
        result.timed_out = timed_out;
        return result;
    }
};

btrfsbackup::BackupRunPlan run_plan() {
    return btrfsbackup::BackupRunPlan{
        .profile_id = "default",
        .run_id = "20260823T120000Z-123-456",
    };
}

btrfsbackup::BackupSourceRunPlan source_plan(const fs::path& root) {
    btrfsbackup::BackupSourceRunPlan source;
    source.source_id = "root";
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

btrfsbackup::BackupRunAction action(btrfsbackup::BackupRunActionKind kind) {
    return btrfsbackup::BackupRunAction{
        .kind = kind,
        .source_id = "root",
    };
}

btrfsbackup::BackupRunAction hook_action(btrfsbackup::BackupRunActionKind kind) {
    return btrfsbackup::BackupRunAction{
        .kind = kind,
        .source_id = "root",
        .hook = btrfsbackup::ProfileHookCommand{
            .program = "/etc/btrfs-backup/hooks.d/prepare-backup",
            .arguments = {"--source", "root"},
            .timeout_seconds = 300,
        },
    };
}

void execute_action(
    btrfsbackup::BackupRunActionEffects& effects,
    const btrfsbackup::BackupRunAction& action,
    const btrfsbackup::BackupSourceRunPlan& source
) {
    btrfsbackup::CancellationToken cancellation;
    effects.execute_action(action, source, run_plan(), cancellation);
}

void test_create_snapshot_writes_pending_marker_and_verifies_readonly_snapshot() {
    fs::path root = test_helpers::test_root("backup-run-action-effects", "create-snapshot");
    btrfsbackup::BackupSourceRunPlan source = source_plan(root);
    FakeBtrfsOperations btrfs;
    btrfs.metadata_by_path[source.local_snapshot_path.string()] = btrfsbackup::SnapshotMetadata{
        .is_subvolume = true,
        .readonly = true,
        .uuid = "local-uuid",
    };
    FakeFileSystem fs_effects;
    btrfsbackup::BackupRunActionEffects effects(btrfs, fs_effects);

    execute_action(effects, action(btrfsbackup::BackupRunActionKind::CreateSnapshot), source);

    test_helpers::expect_eq("mkdir local snapshot dir", fs_effects.calls.at(0), action_path("mkdir", source.local_snapshot_dir));
    test_helpers::expect_eq("snapshot call", btrfs.calls.at(0), "snapshot:" + source.source_subvolume.string() + "->" + source.local_snapshot_path.string());
    test_helpers::expect_eq("metadata call", btrfs.calls.at(1), action_path("metadata", source.local_snapshot_path));
    test_helpers::expect_true("pending marker exists", fs::is_regular_file(source.recovery.marker_path), "pending marker should be written");
}

void test_cleanup_incoming_deletes_subvolumes_and_plain_paths() {
    fs::path root = test_helpers::test_root("backup-run-action-effects", "cleanup-incoming");
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
    btrfsbackup::BackupRunActionEffects effects(btrfs, fs_effects);

    execute_action(effects, action(btrfsbackup::BackupRunActionKind::CleanupIncoming), source);

    test_helpers::expect_true("list incoming", std::find(
        fs_effects.calls.begin(),
        fs_effects.calls.end(),
        action_path("list", source.incoming_source_root)
    ) != fs_effects.calls.end(), "incoming root should be listed");
    test_helpers::expect_true("delete nested subvolume", std::find(
        btrfs.calls.begin(),
        btrfs.calls.end(),
        action_path("delete", nested_subvolume)
    ) != btrfs.calls.end(), "nested subvolume should be deleted before removing its parent directory");
    test_helpers::expect_true("delete plain dir", std::find(
        fs_effects.calls.begin(),
        fs_effects.calls.end(),
        action_path("rmdir", directory)
    ) != fs_effects.calls.end(), "plain directory should be removed after its contents");
    test_helpers::expect_true("delete subvolume", std::find(
        btrfs.calls.begin(),
        btrfs.calls.end(),
        action_path("delete", subvolume)
    ) != btrfs.calls.end(), "subvolume should be deleted with btrfs");
}

void test_production_cleanup_rejects_incoming_symlink_escape() {
    fs::path root = test_helpers::test_root("backup-run-action-effects", "cleanup-symlink");
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
    btrfsbackup::BackupRunActionEffects effects(btrfs, fs_effects, hooks, target);

    test_helpers::expect_validation_error("production cleanup symlink", [&] {
        execute_action(effects, action(btrfsbackup::BackupRunActionKind::CleanupIncoming), source);
    }, "Too many levels of symbolic links");
    test_helpers::expect_true(
        "production cleanup outside preserved",
        fs::is_regular_file(outside / "sentinel"),
        "cleanup followed incoming symlink outside target"
    );

    fs::remove_all(root);
}

void test_verify_commit_retention_and_cleanup_use_existing_helpers() {
    fs::path root = test_helpers::test_root("backup-run-action-effects", "commit-cleanup");
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
    btrfsbackup::BackupRunActionEffects effects(btrfs, fs_effects);

    execute_action(effects, action(btrfsbackup::BackupRunActionKind::VerifyReceived), source);
    execute_action(effects, action(btrfsbackup::BackupRunActionKind::CommitReceived), source);
    execute_action(effects, action(btrfsbackup::BackupRunActionKind::ApplyRemoteRetention), source);
    execute_action(effects, action(btrfsbackup::BackupRunActionKind::ApplyLocalRetention), source);
    execute_action(effects, action(btrfsbackup::BackupRunActionKind::CleanupSource), source);

    test_helpers::expect_true("commit snapshot", std::find(
        btrfs.calls.begin(),
        btrfs.calls.end(),
        "snapshot:" + source.received_snapshot_path.string() + "->" + source.final_remote_snapshot_path.string()
    ) != btrfs.calls.end(), "commit should snapshot received subvolume");
    test_helpers::expect_true("remote retention delete", std::find(
        btrfs.calls.begin(),
        btrfs.calls.end(),
        action_path("delete", root / "remote" / "old")
    ) != btrfs.calls.end(), "remote retention should delete planned snapshot");
    test_helpers::expect_true("local retention delete", std::find(
        btrfs.calls.begin(),
        btrfs.calls.end(),
        action_path("delete", root / "local" / "old")
    ) != btrfs.calls.end(), "local retention should delete planned snapshot");
    test_helpers::expect_true("cleanup received", std::find(
        btrfs.calls.begin(),
        btrfs.calls.end(),
        action_path("delete", source.received_snapshot_path)
    ) != btrfs.calls.end(), "cleanup should delete received subvolume");
}

void test_send_receive_prepares_remote_and_incoming_directories() {
    fs::path root = test_helpers::test_root("backup-run-action-effects", "send-receive");
    btrfsbackup::BackupSourceRunPlan source = source_plan(root);
    FakeBtrfsOperations btrfs;
    FakeFileSystem fs_effects;
    btrfsbackup::BackupRunActionEffects effects(btrfs, fs_effects);

    execute_action(effects, action(btrfsbackup::BackupRunActionKind::SendReceive), source);

    test_helpers::expect_true("remote dir", std::find(
        fs_effects.calls.begin(),
        fs_effects.calls.end(),
        action_path("mkdir", source.remote_snapshot_dir)
    ) != fs_effects.calls.end(), "remote snapshot directory should be created");
    test_helpers::expect_true("incoming run dir", std::find(
        fs_effects.calls.begin(),
        fs_effects.calls.end(),
        action_path("mkdir", source.incoming_run_dir)
    ) != fs_effects.calls.end(), "incoming run directory should be created before receive");
}

void test_pending_recovery_deletes_invalid_remote_snapshot_first() {
    fs::path root = test_helpers::test_root("backup-run-action-effects", "recover-invalid-commit");
    btrfsbackup::BackupSourceRunPlan source = source_plan(root);
    source.recovery.delete_remote_snapshot = true;
    source.recovery.remote_snapshot_path = source.final_remote_snapshot_path;
    source.recovery.delete_local_snapshot = true;
    source.recovery.local_snapshot_path = source.local_snapshot_path;
    FakeBtrfsOperations btrfs;
    FakeFileSystem fs_effects;
    btrfsbackup::BackupRunActionEffects effects(btrfs, fs_effects);

    execute_action(effects, action(btrfsbackup::BackupRunActionKind::RecoverPending), source);

    test_helpers::expect_eq("recovery delete count", std::to_string(btrfs.calls.size()), "2");
    test_helpers::expect_eq("recovery remote first", btrfs.calls.at(0), action_path("delete", source.final_remote_snapshot_path));
    test_helpers::expect_eq("recovery local second", btrfs.calls.at(1), action_path("delete", source.local_snapshot_path));
}

void test_failed_remote_recovery_keeps_local_snapshot_and_marker() {
    fs::path root = test_helpers::test_root("backup-run-action-effects", "recover-invalid-commit-fails");
    btrfsbackup::BackupSourceRunPlan source = source_plan(root);
    source.recovery.delete_remote_snapshot = true;
    source.recovery.remote_snapshot_path = source.final_remote_snapshot_path;
    source.recovery.delete_local_snapshot = true;
    source.recovery.local_snapshot_path = source.local_snapshot_path;
    source.recovery.clear_marker = true;
    btrfsbackup::write_pending_marker(
        root / "state",
        btrfsbackup::PendingMarker{
            .source_name = source.source_id,
            .local_snapshot_path = source.local_snapshot_path.string(),
            .final_snapshot_path = source.final_remote_snapshot_path.string(),
            .run_id = "run-1",
            .timestamp = "2026-08-23T12:00:00Z",
        }
    );

    FakeBtrfsOperations btrfs;
    btrfs.delete_failure_path = source.final_remote_snapshot_path;
    FakeFileSystem fs_effects;
    btrfsbackup::BackupRunActionEffects effects(btrfs, fs_effects);

    test_helpers::expect_validation_error("failed recovery delete", [&] {
        execute_action(effects, action(btrfsbackup::BackupRunActionKind::RecoverPending), source);
    }, "injected subvolume delete failure");

    test_helpers::expect_eq("failed recovery delete count", std::to_string(btrfs.calls.size()), "1");
    test_helpers::expect_eq("failed recovery stops at remote", btrfs.calls.at(0), action_path("delete", source.final_remote_snapshot_path));
    test_helpers::expect_true("failed recovery keeps marker", fs::is_regular_file(source.recovery.marker_path), "pending marker must remain after failed cleanup");
    fs::remove_all(root);
}

void test_hook_actions_use_command_runner_argv() {
    fs::path root = test_helpers::test_root("backup-run-action-effects", "hooks");
    btrfsbackup::BackupSourceRunPlan source = source_plan(root);
    FakeBtrfsOperations btrfs;
    FakeFileSystem fs_effects;
    FakeCommandRunner hooks;
    btrfsbackup::BackupRunActionEffects effects(btrfs, fs_effects, hooks);

    execute_action(effects, hook_action(btrfsbackup::BackupRunActionKind::BeforeSnapshotHook), source);

    test_helpers::expect_eq("hook call count", std::to_string(hooks.calls.size()), "1");
    test_helpers::expect_eq("hook program", hooks.calls.at(0).at(0), "/etc/btrfs-backup/hooks.d/prepare-backup");
    test_helpers::expect_eq("hook arg 1", hooks.calls.at(0).at(1), "--source");
    test_helpers::expect_eq("hook arg 2", hooks.calls.at(0).at(2), "root");
    test_helpers::expect_eq(
        "hook timeout forwarded",
        std::to_string(hooks.controlled_options->timeout.count()),
        std::to_string(std::chrono::minutes(5).count() * 60 * 1000)
    );
}

void test_production_hook_uses_pinned_trusted_descriptor() {
    fs::path root = test_helpers::test_root("backup-run-action-effects", "trusted-hook");
    fs::path hook_root = root / "hooks.d";
    fs::create_directories(hook_root);
    fs::path program = hook_root / "prepare-backup";
    test_helpers::write_file(program, "#!/bin/sh\nexit 0\n");
    chmod(program.c_str(), 0700);

    btrfsbackup::BackupSourceRunPlan source = source_plan(root);
    FakeBtrfsOperations btrfs;
    FakeFileSystem fs_effects;
    FakeCommandRunner hooks;
    btrfsbackup::BackupRunActionEffects effects(
        btrfs,
        fs_effects,
        hooks,
        root,
        hook_root,
        {.allow_current_user_owner = true, .verify_parent_directories = false}
    );
    btrfsbackup::BackupRunAction trusted_hook = hook_action(
        btrfsbackup::BackupRunActionKind::BeforeSnapshotHook
    );
    trusted_hook.hook.program = program.string();

    execute_action(effects, trusted_hook, source);

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

void test_hook_failure_is_reported_as_validation_error() {
    fs::path root = test_helpers::test_root("backup-run-action-effects", "hook-failure");
    btrfsbackup::BackupSourceRunPlan source = source_plan(root);
    FakeBtrfsOperations btrfs;
    FakeFileSystem fs_effects;
    FakeCommandRunner hooks;
    hooks.exit_code = 42;
    btrfsbackup::BackupRunActionEffects effects(btrfs, fs_effects, hooks);

    test_helpers::expect_validation_error("hook failure", [&] {
        execute_action(effects, hook_action(btrfsbackup::BackupRunActionKind::AfterSnapshotHook), source);
    }, "hook failed");
}

void test_hook_timeout_has_stable_error_code() {
    fs::path root = test_helpers::test_root("backup-run-action-effects", "hook-timeout");
    btrfsbackup::BackupSourceRunPlan source = source_plan(root);
    FakeBtrfsOperations btrfs;
    FakeFileSystem fs_effects;
    FakeCommandRunner hooks;
    hooks.timed_out = true;
    btrfsbackup::BackupRunActionEffects effects(btrfs, fs_effects, hooks);
    btrfsbackup::BackupRunAction timed_hook = hook_action(btrfsbackup::BackupRunActionKind::BeforeSnapshotHook);
    timed_hook.hook.timeout_seconds = 17;

    try {
        execute_action(effects, timed_hook, source);
        test_helpers::expect_true("hook timeout throws", false, "timeout should fail the action");
    } catch (const btrfsbackup::CodedValidationError& error) {
        test_helpers::expect_eq("hook timeout code", error.error_code, "hook.before_snapshot_timeout");
        test_helpers::expect_contains("hook timeout message", error.what(), "17 seconds");
    }
}

void test_hook_cancellation_is_not_reported_as_hook_failure() {
    fs::path root = test_helpers::test_root("backup-run-action-effects", "hook-cancel");
    btrfsbackup::BackupSourceRunPlan source = source_plan(root);
    FakeBtrfsOperations btrfs;
    FakeFileSystem fs_effects;
    FakeCommandRunner hooks;
    hooks.cancelled = true;
    btrfsbackup::BackupRunActionEffects effects(btrfs, fs_effects, hooks);

    bool cancelled = false;
    try {
        execute_action(effects, hook_action(btrfsbackup::BackupRunActionKind::AfterSnapshotHook), source);
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
    test_hook_failure_is_reported_as_validation_error();
    test_hook_timeout_has_stable_error_code();
    test_hook_cancellation_is_not_reported_as_hook_failure();

    return test_helpers::finish("backup run action effects tests");
}
