// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/backup_run_serialization.hpp>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace btrfsbackup {

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

std::string backup_run_action_kind_name(BackupRunActionKind kind) {
    switch (kind) {
    case BackupRunActionKind::RecoverPending:
        return "recover-pending";
    case BackupRunActionKind::CleanupIncoming:
        return "cleanup-incoming";
    case BackupRunActionKind::BeforeSnapshotHook:
        return "before-snapshot-hook";
    case BackupRunActionKind::CreateSnapshot:
        return "create-snapshot";
    case BackupRunActionKind::AfterSnapshotHook:
        return "after-snapshot-hook";
    case BackupRunActionKind::SelectParent:
        return "select-parent";
    case BackupRunActionKind::SendReceive:
        return "send-receive";
    case BackupRunActionKind::VerifyReceived:
        return "verify-received";
    case BackupRunActionKind::CommitReceived:
        return "commit-received";
    case BackupRunActionKind::ApplyRemoteRetention:
        return "apply-remote-retention";
    case BackupRunActionKind::ApplyLocalRetention:
        return "apply-local-retention";
    case BackupRunActionKind::CleanupSource:
        return "cleanup-source";
    }
    return "unknown";
}

std::string backup_run_event_kind_name(BackupRunEventKind kind) {
    switch (kind) {
    case BackupRunEventKind::RunStarted:
        return "run-started";
    case BackupRunEventKind::SourceStarted:
        return "source-started";
    case BackupRunEventKind::ActionStarted:
        return "action-started";
    case BackupRunEventKind::TransferProgress:
        return "transfer-progress";
    case BackupRunEventKind::ActionCompleted:
        return "action-completed";
    case BackupRunEventKind::ActionFailed:
        return "action-failed";
    case BackupRunEventKind::CheckpointWritten:
        return "checkpoint-written";
    case BackupRunEventKind::SourceCompleted:
        return "source-completed";
    case BackupRunEventKind::RunCompleted:
        return "run-completed";
    case BackupRunEventKind::RunCancelled:
        return "run-cancelled";
    }
    return "unknown";
}

Json build_backup_run_checkpoint_json(const BackupRunCheckpoint& checkpoint) {
    return {
        {"schemaVersion", 1},
        {"profileId", std::string(checkpoint.profile_id.value())},
        {"runId", std::string(checkpoint.run_id.value())},
        {"sourceId", std::string(checkpoint.source_id.value())},
        {"action", backup_run_action_kind_name(checkpoint.action_kind)},
        {"updatedAt", current_utc_iso_timestamp()},
    };
}

Json build_backup_run_event_json(const BackupRunEvent& event) {
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

} // namespace btrfsbackup
