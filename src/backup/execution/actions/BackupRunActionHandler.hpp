// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/model/BackupRunPlan.hpp>

namespace btrfsbackup {
class CancellationToken;
}

namespace btrfsbackup::backup::execution {
class HookActionHandler;
class RecoveryActionHandler;
class RepositoryActionHandler;
class RetentionActionHandler;
class SnapshotActionHandler;

class IBackupRunActionHandler {
  public:
    virtual ~IBackupRunActionHandler() = default;

    virtual void handle(
        const BackupRunAction& action,
        const BackupRunPlan& run_plan,
        CancellationToken& cancellation
    ) = 0;
};

class BackupRunActionHandler final : public IBackupRunActionHandler {
  public:
    BackupRunActionHandler(
        SnapshotActionHandler& snapshots,
        RecoveryActionHandler& recovery,
        RetentionActionHandler& retention,
        HookActionHandler& hooks,
        RepositoryActionHandler& repository
    );

    void handle(
        const BackupRunAction& action,
        const BackupRunPlan& run_plan,
        CancellationToken& cancellation
    ) override;

  private:
    SnapshotActionHandler& snapshots_;
    RecoveryActionHandler& recovery_;
    RetentionActionHandler& retention_;
    HookActionHandler& hooks_;
    RepositoryActionHandler& repository_;
};

} // namespace btrfsbackup::backup::execution
