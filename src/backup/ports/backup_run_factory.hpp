// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/model/backup_run_event.hpp>
#include <backup/model/backup_run_execution.hpp>
#include <backup/model/backup_run_plan.hpp>
#include <core/cancellation.hpp>

namespace btrfsbackup {

class IBackupRunFactory {
  public:
    virtual ~IBackupRunFactory() = default;

    [[nodiscard]] virtual BackupRunExecutionResult execute(
        BackupRunPlan plan,
        IBackupRunEventSink& events,
        IBackupRunCheckpointStore& checkpoints,
        CancellationToken& cancellation
    ) = 0;
};

} // namespace btrfsbackup
