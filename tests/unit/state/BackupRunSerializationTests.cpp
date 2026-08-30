// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/BackupRunSerialization.hpp>

#include "support/TestHelpers.hpp"

namespace {

btrfsbackup::backup::TransferProgress transfer_progress() {
    return btrfsbackup::backup::TransferProgress{
        .profile_id = btrfsbackup::ProfileId{"default"},
        .run_id = btrfsbackup::RunId{"20260823T120000Z-123-456"},
        .source_id = btrfsbackup::SourceId{"root"},
        .source_index = 1,
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

void test_names_are_stable() {
    test_helpers::expect_eq(
        "action name",
        btrfsbackup::state::backup_run_action_kind_name(btrfsbackup::backup::BackupRunActionKind::CommitReceived),
        "commit-received"
    );
    test_helpers::expect_eq(
        "event name",
        btrfsbackup::state::backup_run_event_kind_name(btrfsbackup::backup::BackupRunEventKind::TransferProgress),
        "transfer-progress"
    );
    test_helpers::expect_eq(
        "run failure event name",
        btrfsbackup::state::backup_run_event_kind_name(btrfsbackup::backup::BackupRunEventKind::RunFailed),
        "run-failed"
    );
    test_helpers::expect_eq(
        "validation event name",
        btrfsbackup::state::backup_run_event_kind_name(btrfsbackup::backup::BackupRunEventKind::TargetValidationCompleted),
        "target-validation-completed"
    );
    test_helpers::expect_eq(
        "operation name",
        btrfsbackup::state::operation_kind_name(btrfsbackup::backup::OperationKind::TargetValidation),
        "target-validation"
    );
}

void test_build_validation_event_json() {
    const btrfsbackup::config::Json started = btrfsbackup::state::build_backup_run_event_json(
        btrfsbackup::backup::RunStarted{
            btrfsbackup::ProfileId{"default"},
            btrfsbackup::RunId{"validation-1"},
            btrfsbackup::backup::OperationKind::TargetValidation,
        }
    );
    const btrfsbackup::config::Json completed = btrfsbackup::state::build_backup_run_event_json(
        btrfsbackup::backup::TargetValidationCompleted{
            btrfsbackup::ProfileId{"default"},
            btrfsbackup::RunId{"validation-1"},
        }
    );

    test_helpers::expect_true("validation start operation", started.at("operationKind") == "target-validation", "wrong operation kind");
    test_helpers::expect_true("validation completed event", completed.at("event") == "target-validation-completed", "wrong terminal event");
    test_helpers::expect_true("validation completed operation", completed.at("operationKind") == "target-validation", "wrong terminal operation kind");

    const btrfsbackup::config::Json failed = btrfsbackup::state::build_backup_run_event_json(
        btrfsbackup::backup::RunFailed{
            .profile_id = btrfsbackup::ProfileId{"default"},
            .run_id = btrfsbackup::RunId{"validation-1"},
            .error_code = btrfsbackup::ErrorCode::BackupFailed,
            .message = "validation failed",
            .operation_kind = btrfsbackup::backup::OperationKind::TargetValidation,
        }
    );
    test_helpers::expect_true("validation failure operation", failed.at("operationKind") == "target-validation", "failed validation was serialized as backup");
}

void test_build_event_json() {
    const btrfsbackup::config::Json data = btrfsbackup::state::build_backup_run_event_json(
        transfer_progress()
    );

    test_helpers::expect_true("schema", data.at("schemaVersion") == 1, "wrong schema");
    test_helpers::expect_true("event", data.at("event") == "transfer-progress", "wrong event");
    test_helpers::expect_true("action", data.at("action") == "send-receive", "wrong action");
    test_helpers::expect_true("source index", data.at("sourceIndex") == 1, "wrong source index");
    test_helpers::expect_true("bytes", data.at("bytesTransferred") == 4096, "wrong bytes");
    test_helpers::expect_true("produced bytes", data.at("bytesProduced") == 8192, "wrong produced bytes");
    test_helpers::expect_true("estimated bytes", data.at("bytesTotalEstimated") == 8192, "wrong estimated bytes");
    test_helpers::expect_true("run bytes", data.at("runBytesTransferred") == 12288, "wrong run bytes");
    test_helpers::expect_true("delta bytes", data.at("deltaBytes") == 1024, "wrong delta bytes");
    test_helpers::expect_true("elapsed", data.at("elapsedMs") == 2000, "wrong elapsed");
    test_helpers::expect_true("speed", data.at("speedBps") == 2048, "wrong speed");
}

void test_build_run_event_json_without_source() {
    const btrfsbackup::backup::RunCompleted run_completed{
        .profile_id = btrfsbackup::ProfileId{"default"},
        .run_id = btrfsbackup::RunId{"20260823T120000Z-123-456"},
    };

    const btrfsbackup::config::Json data = btrfsbackup::state::build_backup_run_event_json(run_completed);
    test_helpers::expect_true("run event source", data.at("sourceId") == "", "run-level event has a source");
    test_helpers::expect_true("run event action", data.at("action") == "", "run-level event has an action");
}

} // namespace

int main() {
    test_names_are_stable();
    test_build_event_json();
    test_build_run_event_json_without_source();
    test_build_validation_event_json();

    return test_helpers::finish("backup run serialization tests");
}
