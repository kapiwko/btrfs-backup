// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include <backup/backup_action_executor.hpp>
#include <backup/backup_run_checkpoint_policy.hpp>
#include <backup/model/backup_run_event.hpp>
#include <backup/model/backup_run_execution.hpp>
#include <backup/model/backup_run_plan.hpp>

namespace btrfsbackup::backup {

class BackupRunExecutor {
  public:
    BackupRunExecutor(
        IBackupActionExecutor& action_executor,
        IBackupRunCheckpointStore& checkpoints
    );

    [[nodiscard]] BackupRunExecutionResult execute(
        const BackupRunPlan& plan,
        IBackupRunEventSink& events,
        CancellationToken& cancellation
    );

  private:
    IBackupActionExecutor& action_executor_;
    BackupRunCheckpointPolicy checkpoint_policy_;
};

} // namespace btrfsbackup::backup
