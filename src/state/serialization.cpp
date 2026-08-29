// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/serialization.hpp>

#include <chrono>

#include <core/runtime_time.hpp>

namespace btrfsbackup::state {

namespace {

struct SerializedEventData {
    btrfsbackup::backup::BackupRunEventKind kind;
    btrfsbackup::backup::OperationKind operation_kind;
    ProfileId profile_id;
    RunId run_id;
    std::optional<SourceId> source_id;
    int source_index = 0;
    std::optional<btrfsbackup::backup::BackupRunActionKind> action_kind;
    btrfsbackup::backup::BackupTransferStage transfer_stage = btrfsbackup::backup::BackupTransferStage::Transferring;
    std::uint64_t bytes_transferred = 0;
    std::uint64_t bytes_produced = 0;
    std::uint64_t bytes_total_estimated = 0;
    std::uint64_t run_bytes_transferred = 0;
    std::uint64_t delta_bytes = 0;
    std::uint64_t elapsed_ms = 0;
    std::uint64_t speed_bps = 0;
    std::optional<ErrorCode> error_code;
    std::string message;
};

SerializedEventData serialized_event_data(const btrfsbackup::backup::BackupRunEvent& event) {
    SerializedEventData data{
        .kind = btrfsbackup::backup::backup_run_event_kind(event),
        .operation_kind = btrfsbackup::backup::backup_run_event_operation_kind(event),
        .profile_id = btrfsbackup::backup::backup_run_event_profile_id(event),
        .run_id = btrfsbackup::backup::backup_run_event_run_id(event),
        .source_id = btrfsbackup::backup::backup_run_event_source_id(event),
        .source_index = btrfsbackup::backup::backup_run_event_source_index(event),
        .action_kind = btrfsbackup::backup::backup_run_event_action_kind(event),
        .transfer_stage = btrfsbackup::backup::BackupTransferStage::Transferring,
        .bytes_transferred = 0,
        .bytes_produced = 0,
        .bytes_total_estimated = 0,
        .run_bytes_transferred = 0,
        .delta_bytes = 0,
        .elapsed_ms = 0,
        .speed_bps = 0,
        .error_code = std::nullopt,
        .message = {},
    };
    if (const auto* progress = std::get_if<btrfsbackup::backup::TransferProgress>(&event)) {
        data.transfer_stage = progress->stage;
        data.bytes_transferred = progress->bytes_transferred;
        data.bytes_produced = progress->bytes_produced;
        data.bytes_total_estimated = progress->bytes_total_estimated;
        data.run_bytes_transferred = progress->run_bytes_transferred;
        data.delta_bytes = progress->delta_bytes;
        data.elapsed_ms = progress->elapsed_ms;
        data.speed_bps = progress->speed_bps;
        data.message = progress->message;
    } else if (const auto* action_failed = std::get_if<btrfsbackup::backup::ActionFailed>(&event)) {
        data.error_code = action_failed->error_code;
        data.message = action_failed->message;
    } else if (const auto* run_failed = std::get_if<btrfsbackup::backup::RunFailed>(&event)) {
        data.error_code = run_failed->error_code;
        data.message = run_failed->message;
    } else if (const auto* cancelled = std::get_if<btrfsbackup::backup::RunCancelled>(&event)) {
        data.error_code = cancelled->error_code;
        data.message = cancelled->message;
    }
    return data;
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
    case btrfsbackup::backup::BackupRunEventKind::TargetValidationCompleted:
        return "target-validation-completed";
    case btrfsbackup::backup::BackupRunEventKind::RunCompleted:
        return "run-completed";
    case btrfsbackup::backup::BackupRunEventKind::RunFailed:
        return "run-failed";
    case btrfsbackup::backup::BackupRunEventKind::RunCancelled:
        return "run-cancelled";
    }
    return "unknown";
}

std::string operation_kind_name(btrfsbackup::backup::OperationKind kind) {
    switch (kind) {
    case btrfsbackup::backup::OperationKind::Backup:
        return "backup";
    case btrfsbackup::backup::OperationKind::TargetValidation:
        return "target-validation";
    case btrfsbackup::backup::OperationKind::Planning:
        return "planning";
    }
    return "backup";
}

btrfsbackup::config::Json build_backup_run_checkpoint_json(const btrfsbackup::backup::BackupRunCheckpoint& checkpoint) {
    return {
        {"schemaVersion", 1},
        {"profileId", std::string(checkpoint.profile_id.value())},
        {"runId", std::string(checkpoint.run_id.value())},
        {"sourceId", std::string(checkpoint.source_id.value())},
        {"action", backup_run_action_kind_name(checkpoint.action_kind)},
        {"updatedAt", format_utc_iso_timestamp(std::chrono::system_clock::now())},
    };
}

btrfsbackup::config::Json build_backup_run_event_json(const btrfsbackup::backup::BackupRunEvent& event) {
    const SerializedEventData data = serialized_event_data(event);
    const std::string source_id = data.source_id.has_value()
        ? std::string(data.source_id->value())
        : std::string{};
    const std::string action = data.action_kind.has_value()
        ? backup_run_action_kind_name(*data.action_kind)
        : std::string{};
    return {
        {"schemaVersion", 1},
        {"event", backup_run_event_kind_name(data.kind)},
        {"operationKind", operation_kind_name(data.operation_kind)},
        {"profileId", std::string(data.profile_id.value())},
        {"runId", std::string(data.run_id.value())},
        {"sourceId", source_id},
        {"sourceIndex", data.source_index},
        {"action", action},
        {"transferStage", data.transfer_stage == btrfsbackup::backup::BackupTransferStage::Sizing ? "sizing" : "transferring"},
        {"bytesTransferred", data.bytes_transferred},
        {"bytesProduced", data.bytes_produced},
        {"bytesTotalEstimated", data.bytes_total_estimated},
        {"runBytesTransferred", data.run_bytes_transferred},
        {"deltaBytes", data.delta_bytes},
        {"elapsedMs", data.elapsed_ms},
        {"speedBps", data.speed_bps},
        {"errorCode", data.error_code.has_value() ? error_code_name(*data.error_code) : ""},
        {"message", data.message},
    };
}

} // namespace btrfsbackup::state
