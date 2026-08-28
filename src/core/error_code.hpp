// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <optional>
#include <string>

namespace btrfsbackup {

enum class ErrorCode {
    BackupFailed,
    BackupCancelled,
    RunnerActionFailed,
    RunnerCancelled,
    RunnerProfileBusy,
    RunnerTargetBusy,
    RunnerStaleRun,
    RunnerRunMismatch,
    TransferFailed,
    TransferProducerFailed,
    TransferConsumerFailed,
    TransferProducerConsumerFailed,
    HookBeforeSnapshotFailed,
    HookBeforeSnapshotTimeout,
    HookAfterSnapshotFailed,
    HookAfterSnapshotTimeout,
    TargetBtrfsUuidMismatch,
    RepositoryRecoveryRequired,
};

std::string error_code_name(ErrorCode code);
std::optional<ErrorCode> error_code_from_name(const std::string& name);

} // namespace btrfsbackup
