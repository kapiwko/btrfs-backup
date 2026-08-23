#include <filesystem>
#include <string>
#include <sys/stat.h>

#include <btrfsbackup/backup_run_persistence.hpp>
#include <btrfsbackup/json_io.hpp>

#include "test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

int mode_of(const fs::path& path) {
    struct stat info {};
    if (stat(path.c_str(), &info) != 0) {
        test_helpers::fail("stat", "cannot stat " + path.string());
        return 0;
    }
    return info.st_mode & 0777;
}

btrfsbackup::BackupRunEvent event(btrfsbackup::BackupRunEventKind kind) {
    return btrfsbackup::BackupRunEvent{
        .kind = kind,
        .profile_id = "default",
        .run_id = "20260823T120000Z-123-456",
        .source_id = "root",
        .action_kind = btrfsbackup::BackupRunActionKind::SendReceive,
        .bytes_transferred = 4096,
        .message = "test message",
    };
}

void test_names_are_stable() {
    test_helpers::expect_eq(
        "action name",
        btrfsbackup::backup_run_action_kind_name(btrfsbackup::BackupRunActionKind::CommitReceived),
        "commit-received"
    );
    test_helpers::expect_eq(
        "event name",
        btrfsbackup::backup_run_event_kind_name(btrfsbackup::BackupRunEventKind::TransferProgress),
        "transfer-progress"
    );
}

void test_build_event_json() {
    btrfsbackup::Json data = btrfsbackup::build_backup_run_event_json(event(btrfsbackup::BackupRunEventKind::TransferProgress));

    test_helpers::expect_true("schema", data.at("schemaVersion") == 1, "wrong schema");
    test_helpers::expect_true("event", data.at("event") == "transfer-progress", "wrong event");
    test_helpers::expect_true("action", data.at("action") == "send-receive", "wrong action");
    test_helpers::expect_true("bytes", data.at("bytesTransferred") == 4096, "wrong bytes");
}

void test_checkpoint_store_writes_private_json_in_state_dir() {
    fs::path root = test_helpers::test_root("backup-run-persistence", "checkpoint");
    btrfsbackup::JsonFileBackupRunCheckpointStore store(root / "state");

    store.write_checkpoint({
        .profile_id = "default",
        .run_id = "20260823T120000Z-123-456",
        .source_id = "root",
        .action_kind = btrfsbackup::BackupRunActionKind::CreateSnapshot,
    });

    fs::path checkpoint = root / "state" / "checkpoint.json";
    btrfsbackup::Json data = btrfsbackup::load_json_file(checkpoint);
    test_helpers::expect_true("checkpoint exists", fs::is_regular_file(checkpoint), "missing checkpoint");
    test_helpers::expect_true("checkpoint action", data.at("action") == "create-snapshot", "wrong action");
    test_helpers::expect_true("state dir mode", mode_of(root / "state") == 0700, "state dir should be private");
    test_helpers::expect_true("checkpoint mode", mode_of(checkpoint) == 0600, "checkpoint should be private");
    fs::remove_all(root);
}

void test_status_sink_writes_current_and_terminal_history() {
    fs::path root = test_helpers::test_root("backup-run-persistence", "status");
    btrfsbackup::StatusBackupRunEventSink sink({
        .status_root = root / "status",
        .history_root = root / "history",
        .profile_name = "Default backup",
        .source_count = 2,
        .started_at = "2026-08-23T12:00:00Z",
    });

    sink.on_backup_run_event(event(btrfsbackup::BackupRunEventKind::ActionStarted));
    fs::path current = root / "status" / "default" / "current.json";
    btrfsbackup::Json current_data = btrfsbackup::load_json_file(current);
    test_helpers::expect_true("current state", current_data.at("state") == "running", "wrong current state");
    test_helpers::expect_true("current phase", current_data.at("phase") == "send-receive", "wrong current phase");
    test_helpers::expect_true("history absent before terminal", !fs::exists(root / "history" / "default"), "history should wait for terminal event");

    sink.on_backup_run_event(event(btrfsbackup::BackupRunEventKind::RunCompleted));
    fs::path history = root / "history" / "default" / "20260823T120000Z-123-456.json";
    btrfsbackup::Json history_data = btrfsbackup::load_json_file(history);
    test_helpers::expect_true("history state", history_data.at("state") == "succeeded", "wrong history state");
    test_helpers::expect_true("history phase", history_data.at("phase") == "succeeded", "wrong history phase");
    test_helpers::expect_true("last history exists", fs::is_regular_file(root / "history" / "default" / "last.json"), "missing last history");
    fs::remove_all(root);
}

void test_hook_failure_status_uses_stable_error_code() {
    fs::path root = test_helpers::test_root("backup-run-persistence", "hook-failure");
    btrfsbackup::StatusBackupRunEventSink sink({
        .status_root = root / "status",
        .history_root = root / "history",
        .profile_name = "Default backup",
        .source_count = 1,
        .started_at = "2026-08-23T12:00:00Z",
    });

    btrfsbackup::BackupRunEvent failed = event(btrfsbackup::BackupRunEventKind::ActionFailed);
    failed.action_kind = btrfsbackup::BackupRunActionKind::BeforeSnapshotHook;
    failed.message = "hook failed: /usr/local/bin/prepare";

    sink.on_backup_run_event(failed);

    btrfsbackup::Json current = btrfsbackup::load_json_file(root / "status" / "default" / "current.json");
    test_helpers::expect_true("hook state", current.at("state") == "failed", "wrong state");
    test_helpers::expect_true("hook phase", current.at("phase") == "before-snapshot-hook", "wrong phase");
    test_helpers::expect_true("hook error code", current.at("errorCode") == "hook.before_snapshot_failed", "wrong error code");
    test_helpers::expect_true("hook recoverable", current.at("recoverable") == true, "hook failures should be recoverable");
    test_helpers::expect_true("hook action", current.at("details").at("action") == "before-snapshot-hook", "wrong action detail");
    test_helpers::expect_true("hook suggested action", current.at("suggestedAction") == "inspect-hook-program", "wrong suggested action");

    fs::remove_all(root);
}

} // namespace

int main() {
    test_names_are_stable();
    test_build_event_json();
    test_checkpoint_store_writes_private_json_in_state_dir();
    test_status_sink_writes_current_and_terminal_history();
    test_hook_failure_status_uses_stable_error_code();

    return test_helpers::finish("backup run persistence tests");
}
