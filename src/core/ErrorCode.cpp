// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <core/ErrorCode.hpp>

namespace btrfsbackup {

std::string error_code_name(ErrorCode code) {
    switch (code) {
    case ErrorCode::BackupFailed:
        return "backup.failed";
    case ErrorCode::BackupCancelled:
        return "backup.cancelled";
    case ErrorCode::RunnerActionFailed:
        return "runner.action_failed";
    case ErrorCode::RunnerCancelled:
        return "runner.cancelled";
    case ErrorCode::RunnerProfileBusy:
        return "runner.profile_busy";
    case ErrorCode::RunnerTargetBusy:
        return "runner.target_busy";
    case ErrorCode::RunnerStaleRun:
        return "runner.stale_run";
    case ErrorCode::RunnerRunMismatch:
        return "runner.run_mismatch";
    case ErrorCode::TransferFailed:
        return "transfer.failed";
    case ErrorCode::TransferProducerFailed:
        return "transfer.producer_failed";
    case ErrorCode::TransferConsumerFailed:
        return "transfer.consumer_failed";
    case ErrorCode::TransferProducerConsumerFailed:
        return "transfer.producer_consumer_failed";
    case ErrorCode::HookBeforeSnapshotFailed:
        return "hook.before_snapshot_failed";
    case ErrorCode::HookBeforeSnapshotTimeout:
        return "hook.before_snapshot_timeout";
    case ErrorCode::HookAfterSnapshotFailed:
        return "hook.after_snapshot_failed";
    case ErrorCode::HookAfterSnapshotTimeout:
        return "hook.after_snapshot_timeout";
    case ErrorCode::TargetBtrfsUuidMismatch:
        return "target.btrfs_uuid_mismatch";
    case ErrorCode::RepositoryRecoveryRequired:
        return "repository.recovery_required";
    case ErrorCode::ConfigurationSaveFailed:
        return "configuration.save_failed";
    case ErrorCode::ConfigurationRollbackIncomplete:
        return "configuration.rollback_incomplete";
    case ErrorCode::CredentialMutationRollbackIncomplete:
        return "credential.mutation_rollback_incomplete";
    case ErrorCode::ConfigurationChanged:
        return "configuration.changed";
    }
    return "backup.failed";
}

std::optional<ErrorCode> error_code_from_name(const std::string& name) {
    if (name == "backup.failed")
        return ErrorCode::BackupFailed;
    if (name == "backup.cancelled")
        return ErrorCode::BackupCancelled;
    if (name == "runner.action_failed")
        return ErrorCode::RunnerActionFailed;
    if (name == "runner.cancelled")
        return ErrorCode::RunnerCancelled;
    if (name == "runner.profile_busy")
        return ErrorCode::RunnerProfileBusy;
    if (name == "runner.target_busy")
        return ErrorCode::RunnerTargetBusy;
    if (name == "runner.stale_run")
        return ErrorCode::RunnerStaleRun;
    if (name == "runner.run_mismatch")
        return ErrorCode::RunnerRunMismatch;
    if (name == "transfer.failed")
        return ErrorCode::TransferFailed;
    if (name == "transfer.producer_failed")
        return ErrorCode::TransferProducerFailed;
    if (name == "transfer.consumer_failed")
        return ErrorCode::TransferConsumerFailed;
    if (name == "transfer.producer_consumer_failed")
        return ErrorCode::TransferProducerConsumerFailed;
    if (name == "hook.before_snapshot_failed")
        return ErrorCode::HookBeforeSnapshotFailed;
    if (name == "hook.before_snapshot_timeout")
        return ErrorCode::HookBeforeSnapshotTimeout;
    if (name == "hook.after_snapshot_failed")
        return ErrorCode::HookAfterSnapshotFailed;
    if (name == "hook.after_snapshot_timeout")
        return ErrorCode::HookAfterSnapshotTimeout;
    if (name == "target.btrfs_uuid_mismatch")
        return ErrorCode::TargetBtrfsUuidMismatch;
    if (name == "repository.recovery_required")
        return ErrorCode::RepositoryRecoveryRequired;
    if (name == "configuration.save_failed")
        return ErrorCode::ConfigurationSaveFailed;
    if (name == "configuration.rollback_incomplete")
        return ErrorCode::ConfigurationRollbackIncomplete;
    if (name == "credential.mutation_rollback_incomplete")
        return ErrorCode::CredentialMutationRollbackIncomplete;
    if (name == "configuration.changed")
        return ErrorCode::ConfigurationChanged;
    return std::nullopt;
}

} // namespace btrfsbackup
