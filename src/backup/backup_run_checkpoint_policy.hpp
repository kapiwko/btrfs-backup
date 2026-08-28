// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/model/backup_run_event.hpp>
#include <backup/model/backup_run_plan.hpp>

namespace btrfsbackup::backup {

class BackupRunCheckpointPolicy {
  public:
    explicit BackupRunCheckpointPolicy(IBackupRunCheckpointStore& checkpoints);

    void after_success(
        const BackupRunAction& action,
        const BackupRunPlan& plan,
        const BackupSourceRunPlan& source,
        IBackupRunEventSink& events
    );

  private:
    IBackupRunCheckpointStore& checkpoints_;
};

} // namespace btrfsbackup::backup
