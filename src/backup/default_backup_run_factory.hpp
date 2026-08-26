// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/action_handlers/backup_run_action_handler.hpp>
#include <backup/ports/backup_run_factory.hpp>
#include <backup/ports/safe_directory.hpp>
#include <backup/transfer/transfer_pipeline.hpp>

namespace btrfsbackup {

class DefaultBackupRunFactory final : public IBackupRunFactory {
  public:
    DefaultBackupRunFactory(
        IBackupRunActionHandler& action_handler,
        ITransferPipeline& transfers,
        const ISafeDirectoryRootFactory& safe_directories
    );

    [[nodiscard]] BackupRunExecutionResult execute(
        BackupRunPlan plan,
        IBackupRunEventSink& events,
        IBackupRunCheckpointStore& checkpoints,
        CancellationToken& cancellation
    ) override;

  private:
    IBackupRunActionHandler& action_handler_;
    ITransferPipeline& transfers_;
    const ISafeDirectoryRootFactory& safe_directories_;
};

} // namespace btrfsbackup
