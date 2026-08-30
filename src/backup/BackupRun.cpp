// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/BackupRun.hpp>

#include <stdexcept>
#include <utility>

namespace btrfsbackup::backup {

BackupRun::BackupRun(
    BackupRunPlan plan,
    IBackupActionExecutor& action_executor,
    IBackupRunCheckpointStore& checkpoints
)
    : plan_(std::move(plan)), executor_(action_executor, checkpoints) {
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
