#include <algorithm>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <btrfsbackup/backup_run_action_effects.hpp>

#include "test_helpers.hpp"

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
    }
};

class FakeFileSystemEffects final : public btrfsbackup::IFileSystemEffects {
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

    btrfsbackup::CommandResult run(const std::vector<std::string>& argv) override {
        calls.push_back(argv);
        return btrfsbackup::CommandResult{
            .exit_code = exit_code,
            .output = {},
        };
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
            .program = "/usr/local/bin/prepare-backup",
            .arguments = {"--source", "root"},
        },
    };
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
    FakeFileSystemEffects fs_effects;
    btrfsbackup::BackupRunActionEffects effects(btrfs, fs_effects);

    effects.execute_action(action(btrfsbackup::BackupRunActionKind::CreateSnapshot), source, run_plan());

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
    FakeBtrfsOperations btrfs;
    btrfs.subvolumes = {subvolume.string()};
    FakeFileSystemEffects fs_effects;
    fs_effects.directories = {directory, subvolume};
    fs_effects.directory_entries[source.incoming_source_root.string()] = {directory, subvolume};
    btrfsbackup::BackupRunActionEffects effects(btrfs, fs_effects);

    effects.execute_action(action(btrfsbackup::BackupRunActionKind::CleanupIncoming), source, run_plan());

    test_helpers::expect_true("list incoming", std::find(
        fs_effects.calls.begin(),
        fs_effects.calls.end(),
        action_path("list", source.incoming_source_root)
    ) != fs_effects.calls.end(), "incoming root should be listed");
    test_helpers::expect_true("delete plain dir", std::find(
        fs_effects.calls.begin(),
        fs_effects.calls.end(),
        action_path("remove-tree", directory)
    ) != fs_effects.calls.end(), "plain directory should be removed recursively");
    test_helpers::expect_true("delete subvolume", std::find(
        btrfs.calls.begin(),
        btrfs.calls.end(),
        action_path("delete", subvolume)
    ) != btrfs.calls.end(), "subvolume should be deleted with btrfs");
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

    FakeFileSystemEffects fs_effects;
    fs_effects.directories = {source.received_snapshot_path, source.incoming_run_dir};
    fs_effects.directory_entries[source.incoming_run_dir.string()] = {};
    btrfsbackup::BackupRunActionEffects effects(btrfs, fs_effects);

    effects.execute_action(action(btrfsbackup::BackupRunActionKind::VerifyReceived), source, run_plan());
    effects.execute_action(action(btrfsbackup::BackupRunActionKind::CommitReceived), source, run_plan());
    effects.execute_action(action(btrfsbackup::BackupRunActionKind::ApplyRemoteRetention), source, run_plan());
    effects.execute_action(action(btrfsbackup::BackupRunActionKind::ApplyLocalRetention), source, run_plan());
    effects.execute_action(action(btrfsbackup::BackupRunActionKind::CleanupSource), source, run_plan());

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
    FakeFileSystemEffects fs_effects;
    btrfsbackup::BackupRunActionEffects effects(btrfs, fs_effects);

    effects.execute_action(action(btrfsbackup::BackupRunActionKind::SendReceive), source, run_plan());

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

void test_hook_actions_use_command_runner_argv() {
    fs::path root = test_helpers::test_root("backup-run-action-effects", "hooks");
    btrfsbackup::BackupSourceRunPlan source = source_plan(root);
    FakeBtrfsOperations btrfs;
    FakeFileSystemEffects fs_effects;
    FakeCommandRunner hooks;
    btrfsbackup::BackupRunActionEffects effects(btrfs, fs_effects, hooks);

    effects.execute_action(hook_action(btrfsbackup::BackupRunActionKind::BeforeSnapshotHook), source, run_plan());

    test_helpers::expect_eq("hook call count", std::to_string(hooks.calls.size()), "1");
    test_helpers::expect_eq("hook program", hooks.calls.at(0).at(0), "/usr/local/bin/prepare-backup");
    test_helpers::expect_eq("hook arg 1", hooks.calls.at(0).at(1), "--source");
    test_helpers::expect_eq("hook arg 2", hooks.calls.at(0).at(2), "root");
}

void test_hook_failure_is_reported_as_validation_error() {
    fs::path root = test_helpers::test_root("backup-run-action-effects", "hook-failure");
    btrfsbackup::BackupSourceRunPlan source = source_plan(root);
    FakeBtrfsOperations btrfs;
    FakeFileSystemEffects fs_effects;
    FakeCommandRunner hooks;
    hooks.exit_code = 42;
    btrfsbackup::BackupRunActionEffects effects(btrfs, fs_effects, hooks);

    test_helpers::expect_validation_error("hook failure", [&] {
        effects.execute_action(hook_action(btrfsbackup::BackupRunActionKind::AfterSnapshotHook), source, run_plan());
    }, "hook failed");
}

} // namespace

int main() {
    test_create_snapshot_writes_pending_marker_and_verifies_readonly_snapshot();
    test_cleanup_incoming_deletes_subvolumes_and_plain_paths();
    test_verify_commit_retention_and_cleanup_use_existing_helpers();
    test_send_receive_prepares_remote_and_incoming_directories();
    test_hook_actions_use_command_runner_argv();
    test_hook_failure_is_reported_as_validation_error();

    return test_helpers::finish("backup run action effects tests");
}
