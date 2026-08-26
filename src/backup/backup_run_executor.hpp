// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <string>

#include <backup/action_handlers/backup_run_action_handler.hpp>
#include <backup/model/backup_run_event.hpp>
#include <backup/model/backup_run_plan.hpp>
#include <backup/transfer/async_transfer.hpp>

namespace btrfsbackup {

enum class BackupRunExecutionOutcome { Completed,
                                       Cancelled };

struct BackupRunExecutionResult {
    BackupRunExecutionOutcome outcome = BackupRunExecutionOutcome::Completed;
    std::size_t actions_completed = 0;
};

class BackupRunExecutor {
public:
  BackupRunExecutor(
      IBackupRunActionHandler& action_handler,
      IAsyncTransferPipeline& transfer_pipeline,
      IBackupRunCheckpointStore& checkpoints
  );

  [[nodiscard]] BackupRunExecutionResult execute(
      const BackupRunPlan& plan,
      IBackupRunEventSink& events,
      CancellationToken& cancellation
  );

private:
  IBackupRunActionHandler& action_handler_;
  IAsyncTransferPipeline& transfer_pipeline_;
  IBackupRunCheckpointStore& checkpoints_;
};

[[nodiscard]] bool backup_run_action_writes_checkpoint(const BackupRunAction& action);

} // namespace btrfsbackup
