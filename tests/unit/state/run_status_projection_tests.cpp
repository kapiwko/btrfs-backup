// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <string>
#include <state/run_status_projection.hpp>
#include <config/model/json_io.hpp>

#include "support/test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

btrfsbackup::BackupRunEvent event(btrfsbackup::BackupRunEventKind kind) {
    return btrfsbackup::BackupRunEvent{
        .kind = kind,
        .profile_id = btrfsbackup::ProfileId{"default"},
        .run_id = btrfsbackup::RunId{"20260823T120000Z-123-456"},
        .source_id = btrfsbackup::SourceId{"root"},
        .source_index = 1,
        .action_kind = btrfsbackup::BackupRunActionKind::SendReceive,
        .bytes_transferred = 4096,
        .bytes_produced = 8192,
        .bytes_total_estimated = 8192,
        .run_bytes_transferred = 12288,
        .delta_bytes = 1024,
        .elapsed_ms = 2000,
        .speed_bps = 2048,
        .message = "test message",
    };
}

void test_public_transfer_progress_excludes_run_details() {
    fs::path root = test_helpers::test_root("backup-run-persistence", "transfer-progress");
    btrfsbackup::RunStatusProjection sink({
        .status_root = root / "status",
        .history_root = root / "history",
        .profile_name = "Default backup",
        .source_count = 2,
        .started_at = "2026-08-23T12:00:00Z",
        .source_names = {{"root", "@home"}, {"home", "@archive"}},
        .target_name = "backupdisk",
    });

    sink.on_backup_run_event(event(btrfsbackup::BackupRunEventKind::TransferProgress));
    btrfsbackup::Json current = btrfsbackup::load_json_file(root / "status" / "default" / "current.json");
    test_helpers::expect_true("progress source hidden", !current.contains("currentSourceName"), "public status exposes source");
    test_helpers::expect_true("progress bytes hidden", !current.contains("bytesProcessed"), "public status exposes byte count");
    test_helpers::expect_true("progress source label", current.at("sourceName") == "@home", "wrong source label");
    test_helpers::expect_true("progress target label", current.at("targetName") == "backupdisk", "wrong target label");
    test_helpers::expect_true("progress source progress", current.at("sourceProgress") == 50, "wrong source progress");
    test_helpers::expect_true("progress eta", current.at("etaSeconds") == 2, "wrong ETA");
    test_helpers::expect_true("progress overall", current.at("overallProgress") == 25, "wrong overall progress");
    test_helpers::expect_true("progress accuracy", current.at("progressAccuracy") == "estimated", "wrong progress accuracy");

    btrfsbackup::BackupRunEvent second = event(btrfsbackup::BackupRunEventKind::TransferProgress);
    second.source_id = btrfsbackup::SourceId{"home"};
    second.source_index = 2;
    sink.on_backup_run_event(second);
    current = btrfsbackup::load_json_file(root / "status" / "default" / "current.json");
    test_helpers::expect_true("second source overall", current.at("overallProgress") == 75, "wrong second-source overall progress");

    btrfsbackup::BackupRunEvent action_completed = second;
    action_completed.kind = btrfsbackup::BackupRunEventKind::ActionCompleted;
    sink.on_backup_run_event(action_completed);
    current = btrfsbackup::load_json_file(root / "status" / "default" / "current.json");
    test_helpers::expect_true("overall remains monotonic", current.at("overallProgress") == 75, "overall progress regressed after transfer");
    fs::remove_all(root);
}

void test_status_sink_writes_current_and_terminal_history() {
    fs::path root = test_helpers::test_root("backup-run-persistence", "status");
    btrfsbackup::RunStatusProjection sink({
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
    test_helpers::expect_true("current phase hidden", !current_data.contains("phase"), "public status exposes phase");
    test_helpers::expect_true("history absent before terminal", !fs::exists(root / "history" / "default"), "history should wait for terminal event");

    btrfsbackup::BackupRunEvent completed = event(btrfsbackup::BackupRunEventKind::RunCompleted);
    completed.source_id = std::nullopt;
    completed.source_index = 0;
    sink.on_backup_run_event(completed);
    fs::path history = root / "history" / "default" / "20260823T120000Z-123-456.json";
    btrfsbackup::Json history_data = btrfsbackup::load_json_file(history);
    test_helpers::expect_true("history state", history_data.at("state") == "succeeded", "wrong history state");
    test_helpers::expect_true("history phase", history_data.at("phase") == "succeeded", "wrong history phase");
    test_helpers::expect_true("last history exists", fs::is_regular_file(root / "history" / "default" / "last.json"), "missing last history");
    fs::remove_all(root);
}

void test_hook_failure_status_uses_stable_error_code() {
    fs::path root = test_helpers::test_root("backup-run-persistence", "hook-failure");
    btrfsbackup::RunStatusProjection sink({
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
    btrfsbackup::Json history = btrfsbackup::load_json_file(root / "history" / "default" / "20260823T120000Z-123-456.json");
    test_helpers::expect_true("hook state", current.at("state") == "failed", "wrong state");
    test_helpers::expect_true("hook public error code", current.at("errorCode") == "backup.failed", "wrong public error code");
    test_helpers::expect_true("hook phase", history.at("phase") == "before-snapshot-hook", "wrong phase");
    test_helpers::expect_true("hook error code", history.at("errorCode") == "hook.before_snapshot_failed", "wrong error code");
    test_helpers::expect_true("hook recoverable", history.at("recoverable") == true, "hook failures should be recoverable");
    test_helpers::expect_true("hook action", history.at("details").at("action") == "before-snapshot-hook", "wrong action detail");
    test_helpers::expect_true("hook suggested action", history.at("suggestedAction") == "inspect-hook-program", "wrong suggested action");

    fs::remove_all(root);
}

void test_repository_recovery_required_status_is_actionable() {
    fs::path root = test_helpers::test_root("backup-run-persistence", "repository-recovery");
    btrfsbackup::RunStatusProjection sink({
        .status_root = root / "status",
        .history_root = root / "history",
        .profile_name = "Default backup",
        .source_count = 1,
        .started_at = "2026-08-23T12:00:00Z",
    });

    btrfsbackup::BackupRunEvent failed = event(btrfsbackup::BackupRunEventKind::ActionFailed);
    failed.action_kind = btrfsbackup::BackupRunActionKind::CommitReceived;
    failed.error_code = btrfsbackup::ErrorCode::RepositoryRecoveryRequired;
    failed.message = "commit verification failed; cleanup failed; repository requires recovery";

    sink.on_backup_run_event(failed);

    btrfsbackup::Json current = btrfsbackup::load_json_file(root / "status" / "default" / "current.json");
    btrfsbackup::Json history = btrfsbackup::load_json_file(root / "history" / "default" / "20260823T120000Z-123-456.json");
    test_helpers::expect_true("recovery state", current.at("state") == "failed", "wrong state");
    test_helpers::expect_true("recovery public error code", current.at("errorCode") == "backup.failed", "wrong public error code");
    test_helpers::expect_true("recovery error code", history.at("errorCode") == "repository.recovery_required", "wrong error code");
    test_helpers::expect_true("recovery recoverable", history.at("recoverable") == true, "repository recovery should be recoverable");
    test_helpers::expect_true("recovery suggested action", history.at("suggestedAction") == "run-backup-recovery", "wrong suggested action");

    fs::remove_all(root);
}

} // namespace

int main() {
    test_status_sink_writes_current_and_terminal_history();
    test_public_transfer_progress_excludes_run_details();
    test_hook_failure_status_uses_stable_error_code();
    test_repository_recovery_required_status_is_actionable();

    return test_helpers::finish("run status projection tests");
}
