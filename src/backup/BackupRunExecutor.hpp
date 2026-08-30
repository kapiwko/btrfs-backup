// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include <backup/BackupActionExecutor.hpp>
#include <backup/BackupRunCheckpointPolicy.hpp>
#include <backup/model/BackupRunExecution.hpp>
#include <backup/model/BackupRunPlan.hpp>
#include <backup/ports/IBackupRunEventSink.hpp>

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
