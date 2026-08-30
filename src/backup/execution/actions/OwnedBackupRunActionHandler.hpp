// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>

#include <backup/execution/actions/BackupRunActionHandler.hpp>
#include <backup/execution/actions/HookActionHandler.hpp>
#include <backup/execution/actions/RecoveryActionHandler.hpp>
#include <backup/execution/actions/RepositoryActionHandler.hpp>
#include <backup/execution/actions/RetentionActionHandler.hpp>
#include <backup/execution/actions/SnapshotActionHandler.hpp>
#include <backup/ports/SafeDirectory.hpp>

namespace btrfsbackup::backup::execution {

class OwnedBackupRunActionHandler final : public IBackupRunActionHandler {
  public:
    OwnedBackupRunActionHandler(
        IBtrfsOperations& btrfs,
        IFileSystem& filesystem,
        ICommandRunner& commands,
        IPendingMarkerStore& pending_markers,
        IClock& clock,
        const ISafeDirectoryRootFactory& safe_directories,
        const ITrustedExecutableResolver& hook_executables,
        const BackupRunPlan& plan
    );

    void handle(
        const BackupRunAction& action,
        const BackupRunPlan& plan,
        CancellationToken& cancellation
    ) override;

  private:
    SnapshotActionHandler snapshots_;
    RecoveryActionHandler recovery_;
    RetentionActionHandler retention_;
    HookActionHandler hooks_;
    std::unique_ptr<ISafeDirectoryRoot> local_repository_root_;
    std::unique_ptr<ISafeDirectoryRoot> target_repository_root_;
    RepositoryActionHandler repository_;
    BackupRunActionHandler dispatcher_;
};

} // namespace btrfsbackup::backup::execution
