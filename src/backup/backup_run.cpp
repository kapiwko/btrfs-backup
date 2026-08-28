// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/backup_run.hpp>

#include <stdexcept>
#include <utility>

namespace btrfsbackup::backup {

BackupRun::BackupRun(
    BackupRunPlan plan,
    IBackupRunActionHandler& action_handler,
    btrfsbackup::backup::transfer::IAsyncTransferPipeline& transfer_pipeline,
    IBackupRunCheckpointStore& checkpoints,
    const ISafeDirectoryRootFactory& safe_directories
)
    : plan_(std::move(plan)),
      executor_(action_handler, transfer_pipeline, checkpoints, safe_directories) {
}

const BackupRunPlan& BackupRun::plan() const noexcept {
    return plan_;
}

bool BackupRun::started() const noexcept {
    return started_;
}

BackupRunExecutionResult BackupRun::execute(
    IBackupRunEventSink& events,
    CancellationToken& cancellation
) {
    if (started_) {
        throw std::logic_error("backup run cannot be executed more than once");
    }
    started_ = true;
    return executor_.execute(plan_, events, cancellation);
}

} // namespace btrfsbackup::backup
