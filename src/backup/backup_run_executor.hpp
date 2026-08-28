// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include <backup/action_handlers/backup_run_action_handler.hpp>
#include <backup/backup_run_checkpoint_policy.hpp>
#include <backup/model/backup_run_event.hpp>
#include <backup/model/backup_run_execution.hpp>
#include <backup/model/backup_run_plan.hpp>
#include <backup/transfer/transfer_coordinator.hpp>

namespace btrfsbackup::backup {

class BackupRunExecutor {
  public:
    BackupRunExecutor(
        IBackupRunActionHandler& action_handler,
        btrfsbackup::backup::transfer::IAsyncTransferPipeline& transfer_pipeline,
        IBackupRunCheckpointStore& checkpoints,
        const ISafeDirectoryRootFactory& safe_directories
    );

    [[nodiscard]] BackupRunExecutionResult execute(
        const BackupRunPlan& plan,
        IBackupRunEventSink& events,
        CancellationToken& cancellation
    );

  private:
    IBackupRunActionHandler& action_handler_;
    btrfsbackup::backup::transfer::TransferCoordinator transfer_coordinator_;
    BackupRunCheckpointPolicy checkpoint_policy_;
};

} // namespace btrfsbackup::backup
