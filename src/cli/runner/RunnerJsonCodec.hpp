// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include <backup/model/BackupExecution.hpp>

namespace btrfsbackup::cli::runner {

struct EncodedRunnerResponse {
    std::string output;
    int exit_code = 0;
};

[[nodiscard]] EncodedRunnerResponse encode_runner_plan(
    const btrfsbackup::backup::BackupRunPlan& plan
);
[[nodiscard]] EncodedRunnerResponse encode_runner_execution(
    const btrfsbackup::backup::BackupExecutionResult& result
);
[[nodiscard]] EncodedRunnerResponse encode_runner_cancellation(
    const btrfsbackup::backup::CancelBackupResult& result
);

} // namespace btrfsbackup::cli::runner
