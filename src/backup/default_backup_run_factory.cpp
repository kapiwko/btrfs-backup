// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/default_backup_run_factory.hpp>

#include <utility>

#include <backup/backup_run.hpp>

namespace btrfsbackup::backup {

DefaultBackupRunFactory::DefaultBackupRunFactory(
    IBackupRunActionHandler& action_handler,
    btrfsbackup::backup::transfer::ITransferPipeline& transfers,
    const ISafeDirectoryRootFactory& safe_directories
)
    : action_handler_(action_handler), transfers_(transfers), safe_directories_(safe_directories) {
}

BackupRunExecutionResult DefaultBackupRunFactory::execute(
    BackupRunPlan plan,
    IBackupRunEventSink& events,
    IBackupRunCheckpointStore& checkpoints,
    CancellationToken& cancellation
) {
    btrfsbackup::backup::transfer::ThreadedAsyncTransferPipeline async_transfers(transfers_);
    BackupRun run(std::move(plan), action_handler_, async_transfers, checkpoints, safe_directories_);
    return run.execute(events, cancellation);
}

} // namespace btrfsbackup::backup
