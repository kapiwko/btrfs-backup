// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/execution/BackupRunExecutor.hpp>

namespace btrfsbackup::backup::execution {

class BackupRun {
  public:
    BackupRun(
        BackupRunPlan plan,
        IBackupActionExecutor& action_executor,
        IBackupRunCheckpointStore& checkpoints
    );

    [[nodiscard]] const BackupRunPlan& plan() const noexcept;
    [[nodiscard]] bool started() const noexcept;

    [[nodiscard]] BackupRunExecutionResult execute(
        IBackupRunEventSink& events,
        CancellationToken& cancellation
    );

  private:
    BackupRunPlan plan_;
    BackupRunExecutor executor_;
    bool started_ = false;
};

} // namespace btrfsbackup::backup::execution
