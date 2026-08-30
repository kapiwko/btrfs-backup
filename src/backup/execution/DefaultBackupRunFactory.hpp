// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/ports/BackupRunActionHandlerFactory.hpp>
#include <backup/ports/IBackupRunFactory.hpp>
#include <backup/ports/SafeDirectory.hpp>
#include <backup/transfer/ITransferPipeline.hpp>

namespace btrfsbackup::backup::execution {

class DefaultBackupRunFactory final : public IBackupRunFactory {
  public:
    DefaultBackupRunFactory(
        IBackupRunActionHandlerFactory& action_handlers,
        btrfsbackup::backup::transfer::ITransferPipeline& transfers,
        const ISafeDirectoryRootFactory& safe_directories
    );

    [[nodiscard]] BackupRunExecutionResult execute(
        BackupRunPlan plan,
        IBackupRunEventSink& events,
        IBackupRunCheckpointStore& checkpoints,
        CancellationToken& cancellation
    ) override;

  private:
    IBackupRunActionHandlerFactory& action_handlers_;
    btrfsbackup::backup::transfer::ITransferPipeline& transfers_;
    const ISafeDirectoryRootFactory& safe_directories_;
};

} // namespace btrfsbackup::backup::execution
