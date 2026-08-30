// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>

#include <backup/action_handlers/BackupRunActionHandler.hpp>
#include <backup/action_handlers/HookActionHandler.hpp>
#include <backup/action_handlers/RecoveryActionHandler.hpp>
#include <backup/action_handlers/RepositoryActionHandler.hpp>
#include <backup/action_handlers/RetentionActionHandler.hpp>
#include <backup/action_handlers/SnapshotActionHandler.hpp>
#include <backup/ports/SafeDirectory.hpp>

namespace btrfsbackup::backup {

class OwnedBackupRunActionHandler final : public IBackupRunActionHandler {
  public:
    OwnedBackupRunActionHandler(
        IBtrfsOperations& btrfs,
        IFileSystem& filesystem,
        ICommandRunner& commands,
        IPendingMarkerStore& pending_markers,
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

} // namespace btrfsbackup::backup
