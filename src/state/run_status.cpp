// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/run_status.hpp>

namespace btrfsbackup {

std::string run_state_name(RunState state) {
    switch (state) {
        case RunState::Running: return "running";
        case RunState::Succeeded: return "succeeded";
        case RunState::Failed: return "failed";
        case RunState::Cancelled: return "cancelled";
        case RunState::Skipped: return "skipped";
    }
    return "running";
}

std::string run_phase_name(RunPhase phase) {
    switch (phase) {
        case RunPhase::RunStarted: return "run-started";
        case RunPhase::SourceStarted: return "source-started";
        case RunPhase::RecoverPending: return "recover-pending";
        case RunPhase::CleanupIncoming: return "cleanup-incoming";
        case RunPhase::BeforeSnapshotHook: return "before-snapshot-hook";
        case RunPhase::CreateSnapshot: return "create-snapshot";
        case RunPhase::AfterSnapshotHook: return "after-snapshot-hook";
        case RunPhase::SelectParent: return "select-parent";
        case RunPhase::SendReceive: return "send-receive";
        case RunPhase::Transferring: return "transferring";
        case RunPhase::VerifyReceived: return "verify-received";
        case RunPhase::CommitReceived: return "commit-received";
        case RunPhase::ApplyRemoteRetention: return "apply-remote-retention";
        case RunPhase::ApplyLocalRetention: return "apply-local-retention";
        case RunPhase::CleanupSource: return "cleanup-source";
        case RunPhase::SourceCompleted: return "source-completed";
        case RunPhase::Succeeded: return "succeeded";
        case RunPhase::Cancelled: return "cancelled";
        case RunPhase::Skipped: return "skipped";
        case RunPhase::ValidatingTarget: return "validating-target";
    }
    return "run-started";
}

std::string progress_accuracy_name(ProgressAccuracy accuracy) {
    switch (accuracy) {
        case ProgressAccuracy::Indeterminate: return "indeterminate";
        case ProgressAccuracy::Estimated: return "estimated";
    }
    return "indeterminate";
}

std::string error_code_name(ErrorCode code) {
    switch (code) {
        case ErrorCode::BackupFailed: return "backup.failed";
        case ErrorCode::BackupCancelled: return "backup.cancelled";
        case ErrorCode::RunnerActionFailed: return "runner.action_failed";
        case ErrorCode::RunnerCancelled: return "runner.cancelled";
        case ErrorCode::RunnerProfileBusy: return "runner.profile_busy";
        case ErrorCode::RunnerTargetBusy: return "runner.target_busy";
        case ErrorCode::TransferFailed: return "transfer.failed";
        case ErrorCode::TransferProducerFailed: return "transfer.producer_failed";
        case ErrorCode::TransferConsumerFailed: return "transfer.consumer_failed";
        case ErrorCode::TransferProducerConsumerFailed: return "transfer.producer_consumer_failed";
        case ErrorCode::HookBeforeSnapshotFailed: return "hook.before_snapshot_failed";
        case ErrorCode::HookBeforeSnapshotTimeout: return "hook.before_snapshot_timeout";
        case ErrorCode::HookAfterSnapshotFailed: return "hook.after_snapshot_failed";
        case ErrorCode::HookAfterSnapshotTimeout: return "hook.after_snapshot_timeout";
        case ErrorCode::TargetBtrfsUuidMismatch: return "target.btrfs_uuid_mismatch";
        case ErrorCode::RepositoryRecoveryRequired: return "repository.recovery_required";
    }
    return "backup.failed";
}

std::optional<ErrorCode> error_code_from_name(const std::string& name) {
    if (name == "backup.failed") return ErrorCode::BackupFailed;
    if (name == "backup.cancelled") return ErrorCode::BackupCancelled;
    if (name == "runner.action_failed") return ErrorCode::RunnerActionFailed;
    if (name == "runner.cancelled") return ErrorCode::RunnerCancelled;
    if (name == "runner.profile_busy") return ErrorCode::RunnerProfileBusy;
    if (name == "runner.target_busy") return ErrorCode::RunnerTargetBusy;
    if (name == "transfer.failed") return ErrorCode::TransferFailed;
    if (name == "transfer.producer_failed") return ErrorCode::TransferProducerFailed;
    if (name == "transfer.consumer_failed") return ErrorCode::TransferConsumerFailed;
    if (name == "transfer.producer_consumer_failed") return ErrorCode::TransferProducerConsumerFailed;
    if (name == "hook.before_snapshot_failed") return ErrorCode::HookBeforeSnapshotFailed;
    if (name == "hook.before_snapshot_timeout") return ErrorCode::HookBeforeSnapshotTimeout;
    if (name == "hook.after_snapshot_failed") return ErrorCode::HookAfterSnapshotFailed;
    if (name == "hook.after_snapshot_timeout") return ErrorCode::HookAfterSnapshotTimeout;
    if (name == "target.btrfs_uuid_mismatch") return ErrorCode::TargetBtrfsUuidMismatch;
    if (name == "repository.recovery_required") return ErrorCode::RepositoryRecoveryRequired;
    return std::nullopt;
}

} // namespace btrfsbackup
