// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/serialization.hpp>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace btrfsbackup::state {

namespace {

std::string current_utc_iso_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&time, &tm);

    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

} // namespace

std::string backup_run_action_kind_name(btrfsbackup::backup::BackupRunActionKind kind) {
    switch (kind) {
    case btrfsbackup::backup::BackupRunActionKind::RecoverPending:
        return "recover-pending";
    case btrfsbackup::backup::BackupRunActionKind::CleanupIncoming:
        return "cleanup-incoming";
    case btrfsbackup::backup::BackupRunActionKind::BeforeSnapshotHook:
        return "before-snapshot-hook";
    case btrfsbackup::backup::BackupRunActionKind::CreateSnapshot:
        return "create-snapshot";
    case btrfsbackup::backup::BackupRunActionKind::AfterSnapshotHook:
        return "after-snapshot-hook";
    case btrfsbackup::backup::BackupRunActionKind::SendReceive:
        return "send-receive";
    case btrfsbackup::backup::BackupRunActionKind::VerifyReceived:
        return "verify-received";
    case btrfsbackup::backup::BackupRunActionKind::CommitReceived:
        return "commit-received";
    case btrfsbackup::backup::BackupRunActionKind::ApplyRemoteRetention:
        return "apply-remote-retention";
    case btrfsbackup::backup::BackupRunActionKind::ApplyLocalRetention:
        return "apply-local-retention";
    case btrfsbackup::backup::BackupRunActionKind::CleanupSource:
        return "cleanup-source";
    }
    return "unknown";
}

std::string backup_run_event_kind_name(btrfsbackup::backup::BackupRunEventKind kind) {
    switch (kind) {
    case btrfsbackup::backup::BackupRunEventKind::RunStarted:
        return "run-started";
    case btrfsbackup::backup::BackupRunEventKind::SourceStarted:
        return "source-started";
    case btrfsbackup::backup::BackupRunEventKind::ActionStarted:
        return "action-started";
    case btrfsbackup::backup::BackupRunEventKind::TransferProgress:
        return "transfer-progress";
    case btrfsbackup::backup::BackupRunEventKind::ActionCompleted:
        return "action-completed";
    case btrfsbackup::backup::BackupRunEventKind::ActionFailed:
        return "action-failed";
    case btrfsbackup::backup::BackupRunEventKind::CheckpointWritten:
        return "checkpoint-written";
    case btrfsbackup::backup::BackupRunEventKind::SourceCompleted:
        return "source-completed";
    case btrfsbackup::backup::BackupRunEventKind::RunCompleted:
        return "run-completed";
    case btrfsbackup::backup::BackupRunEventKind::RunCancelled:
        return "run-cancelled";
    }
    return "unknown";
}

btrfsbackup::config::Json build_backup_run_checkpoint_json(const btrfsbackup::backup::BackupRunCheckpoint& checkpoint) {
    return {
        {"schemaVersion", 1},
        {"profileId", std::string(checkpoint.profile_id.value())},
        {"runId", std::string(checkpoint.run_id.value())},
        {"sourceId", std::string(checkpoint.source_id.value())},
        {"action", backup_run_action_kind_name(checkpoint.action_kind)},
        {"updatedAt", current_utc_iso_timestamp()},
    };
}

btrfsbackup::config::Json build_backup_run_event_json(const btrfsbackup::backup::BackupRunEvent& event) {
    const std::string source_id = event.source_id.has_value()
        ? std::string(event.source_id->value())
        : std::string{};
    return {
        {"schemaVersion", 1},
        {"event", backup_run_event_kind_name(event.kind)},
        {"profileId", std::string(event.profile_id.value())},
        {"runId", std::string(event.run_id.value())},
        {"sourceId", source_id},
        {"sourceIndex", event.source_index},
        {"action", backup_run_action_kind_name(event.action_kind)},
        {"bytesTransferred", event.bytes_transferred},
        {"bytesProduced", event.bytes_produced},
        {"bytesTotalEstimated", event.bytes_total_estimated},
        {"runBytesTransferred", event.run_bytes_transferred},
        {"deltaBytes", event.delta_bytes},
        {"elapsedMs", event.elapsed_ms},
        {"speedBps", event.speed_bps},
        {"errorCode", event.error_code.has_value() ? error_code_name(*event.error_code) : ""},
        {"message", event.message},
    };
}

} // namespace btrfsbackup::state
