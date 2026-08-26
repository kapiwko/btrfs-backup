// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <string>

#include <backup/backup_run_event.hpp>
#include <backup/backup_run_plan.hpp>
#include <backup/transfer/async_transfer.hpp>

namespace btrfsbackup {

class IBackupRunActionEffects {
public:
    virtual ~IBackupRunActionEffects() = default;
    virtual void execute_action(
        const BackupRunAction& action,
        const BackupRunPlan& run_plan,
        CancellationToken& cancellation
    ) = 0;
};

struct BackupRunExecutionResult {
    bool completed = false;
    bool cancelled = false;
    std::size_t actions_completed = 0;
};

class BackupRunExecutor {
public:
    BackupRunExecutor(
        IBackupRunActionEffects& action_effects,
        IAsyncTransferPipeline& transfer_pipeline,
        IBackupRunCheckpointStore& checkpoints
    );

    BackupRunExecutionResult execute(
        const BackupRunPlan& plan,
        IBackupRunEventSink& events,
        CancellationToken& cancellation
    );

private:
    IBackupRunActionEffects& action_effects_;
    IAsyncTransferPipeline& transfer_pipeline_;
    IBackupRunCheckpointStore& checkpoints_;
};

bool backup_run_action_writes_checkpoint(const BackupRunAction& action);

} // namespace btrfsbackup
