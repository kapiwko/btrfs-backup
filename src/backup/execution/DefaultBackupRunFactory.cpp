// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/execution/DefaultBackupRunFactory.hpp>

#include <utility>

#include <backup/execution/actions/BackupRunActionHandler.hpp>
#include <backup/execution/BackupActionExecutor.hpp>
#include <backup/execution/BackupRun.hpp>

namespace btrfsbackup::backup::execution {

DefaultBackupRunFactory::DefaultBackupRunFactory(
    IBackupRunActionHandlerFactory& action_handlers,
    btrfsbackup::backup::transfer::ITransferPipeline& transfers,
    const ISafeDirectoryRootFactory& safe_directories
)
    : action_handlers_(action_handlers), transfers_(transfers), safe_directories_(safe_directories) {
}

BackupRunExecutionResult DefaultBackupRunFactory::execute(
    BackupRunPlan plan,
    IBackupRunEventSink& events,
    IBackupRunCheckpointStore& checkpoints,
    CancellationToken& cancellation
) {
    std::unique_ptr<IBackupRunActionHandler> action_handler = action_handlers_.create(plan);
    btrfsbackup::backup::transfer::ThreadedAsyncTransferPipeline async_transfers(transfers_);
    BackupActionExecutor action_executor(*action_handler, async_transfers, safe_directories_);
    BackupRun run(std::move(plan), action_executor, checkpoints);
    return run.execute(events, cancellation);
}

} // namespace btrfsbackup::backup::execution
