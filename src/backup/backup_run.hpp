// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/backup_run_executor.hpp>

namespace btrfsbackup {

class BackupRun {
public:
    BackupRun(
        BackupRunPlan plan,
        IBackupRunActionEffects& action_effects,
        IAsyncTransferPipeline& transfer_pipeline,
        IBackupRunCheckpointStore& checkpoints
    );

    const BackupRunPlan& plan() const noexcept;
    bool started() const noexcept;

    BackupRunExecutionResult execute(
        IBackupRunEventSink& events,
        CancellationToken& cancellation
    );

private:
    BackupRunPlan plan_;
    BackupRunExecutor executor_;
    bool started_ = false;
};

} // namespace btrfsbackup
