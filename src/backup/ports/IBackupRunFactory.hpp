// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/model/BackupRunExecution.hpp>
#include <backup/model/BackupRunPlan.hpp>
#include <backup/ports/IBackupRunCheckpointStore.hpp>
#include <backup/ports/IBackupRunEventSink.hpp>
#include <core/Cancellation.hpp>

namespace btrfsbackup::backup {

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

} // namespace btrfsbackup::backup
