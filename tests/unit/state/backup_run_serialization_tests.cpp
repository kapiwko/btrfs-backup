// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <optional>

#include <state/serialization.hpp>

#include "support/test_helpers.hpp"

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
    const btrfsbackup::Json data = btrfsbackup::build_backup_run_event_json(
        event(btrfsbackup::BackupRunEventKind::TransferProgress)
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
    btrfsbackup::BackupRunEvent run_completed = event(btrfsbackup::BackupRunEventKind::RunCompleted);
    run_completed.source_id = std::nullopt;
    run_completed.source_index = 0;

    const btrfsbackup::Json data = btrfsbackup::build_backup_run_event_json(run_completed);
    test_helpers::expect_true("run event source", data.at("sourceId") == "", "run-level event has a source");
}

} // namespace

int main() {
    test_names_are_stable();
    test_build_event_json();
    test_build_run_event_json_without_source();

    return test_helpers::finish("backup run serialization tests");
}
