// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <string>
#include <variant>

#include <backup/model/backup_run_plan.hpp>
#include <core/error_code.hpp>
#include <core/identifiers.hpp>

namespace btrfsbackup::backup {

struct BackupRequest {
    ProfileId profile_id;
    bool force = false;
    bool validate_only = false;
};

struct BackupPlanRequest {
    ProfileId profile_id;
    bool mount_target = false;
};

struct CancellationRequest {
    ProfileId profile_id;
    RunId run_id;
};

enum class CancellationRequestOutcome {
    Accepted,
    StaleRun,
    RunMismatch,
};

struct CancellationAccepted {
    ProfileId profile_id;
    RunId run_id;
};

struct CancellationStaleRun {
    ProfileId profile_id;
    RunId run_id;
};

struct CancellationRunMismatch {
    ProfileId profile_id;
    RunId run_id;
};

using CancelBackupResult = std::variant<CancellationAccepted, CancellationStaleRun, CancellationRunMismatch>;

struct BackupExecutionCompleted {
    BackupRunPlan plan;
    std::size_t actions_completed = 0;
};

struct BackupExecutionSkipped {
    BackupRunPlan plan;
};

struct BackupExecutionCancelled {
    BackupRunPlan plan;
    std::size_t actions_completed = 0;
};

struct BackupExecutionBusy {
    ProfileId profile_id;
    RunId run_id;
    ErrorCode error_code;
    std::string error_message;
};

struct BackupExecutionValidated {
    BackupRunPlan plan;
};

struct BackupExecutionFailed {
    ProfileId profile_id;
    RunId run_id;
    ErrorCode error_code;
    std::string error_message;
    std::size_t actions_completed = 0;
};

using BackupExecutionResult = std::variant<
    BackupExecutionCompleted,
    BackupExecutionSkipped,
    BackupExecutionCancelled,
    BackupExecutionBusy,
    BackupExecutionValidated,
    BackupExecutionFailed>;

} // namespace btrfsbackup::backup
