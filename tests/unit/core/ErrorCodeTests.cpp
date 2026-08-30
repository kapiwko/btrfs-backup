// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <string>
#include <utility>

#include <core/ErrorCode.hpp>

#include "support/TestHelpers.hpp"

namespace {

void test_error_code_names_are_stable_and_parseable() {
    using btrfsbackup::ErrorCode;
    const std::array codes{
        std::pair{ErrorCode::BackupFailed, "backup.failed"},
        std::pair{ErrorCode::BackupCancelled, "backup.cancelled"},
        std::pair{ErrorCode::RunnerActionFailed, "runner.action_failed"},
        std::pair{ErrorCode::RunnerCancelled, "runner.cancelled"},
        std::pair{ErrorCode::RunnerProfileBusy, "runner.profile_busy"},
        std::pair{ErrorCode::RunnerTargetBusy, "runner.target_busy"},
        std::pair{ErrorCode::RunnerStaleRun, "runner.stale_run"},
        std::pair{ErrorCode::RunnerRunMismatch, "runner.run_mismatch"},
        std::pair{ErrorCode::TransferFailed, "transfer.failed"},
        std::pair{ErrorCode::TransferProducerFailed, "transfer.producer_failed"},
        std::pair{ErrorCode::TransferConsumerFailed, "transfer.consumer_failed"},
        std::pair{ErrorCode::TransferProducerConsumerFailed, "transfer.producer_consumer_failed"},
        std::pair{ErrorCode::HookBeforeSnapshotFailed, "hook.before_snapshot_failed"},
        std::pair{ErrorCode::HookBeforeSnapshotTimeout, "hook.before_snapshot_timeout"},
        std::pair{ErrorCode::HookAfterSnapshotFailed, "hook.after_snapshot_failed"},
        std::pair{ErrorCode::HookAfterSnapshotTimeout, "hook.after_snapshot_timeout"},
        std::pair{ErrorCode::TargetBtrfsUuidMismatch, "target.btrfs_uuid_mismatch"},
        std::pair{ErrorCode::RepositoryRecoveryRequired, "repository.recovery_required"},
        std::pair{ErrorCode::ConfigurationSaveFailed, "configuration.save_failed"},
        std::pair{ErrorCode::ConfigurationRollbackIncomplete, "configuration.rollback_incomplete"},
        std::pair{ErrorCode::ConfigurationChanged, "configuration.changed"},
    };
    for (const auto& [code, name] : codes) {
        test_helpers::expect_eq("error code name " + std::string(name), btrfsbackup::error_code_name(code), name);
        test_helpers::expect_true(
            "parse error code " + std::string(name),
            btrfsbackup::error_code_from_name(name) == code,
            "known code was not parsed"
        );
    }
    test_helpers::expect_true(
        "unknown error code",
        !btrfsbackup::error_code_from_name("future.unknown").has_value(),
        "unknown code should not be accepted"
    );
}

} // namespace

int main() {
    test_error_code_names_are_stable_and_parseable();
    return test_helpers::finish("error code tests");
}
