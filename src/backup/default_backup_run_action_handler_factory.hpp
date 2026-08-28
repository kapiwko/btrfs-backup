// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/ports/backup_run_action_handler_factory.hpp>

namespace btrfsbackup::backup {

class IBtrfsOperations;
class ICommandRunner;
class IFileSystem;
class IPendingMarkerStore;
class ISafeDirectoryRootFactory;
class ITrustedExecutableResolver;

class DefaultBackupRunActionHandlerFactory final : public IBackupRunActionHandlerFactory {
  public:
    DefaultBackupRunActionHandlerFactory(
        IBtrfsOperations& btrfs,
        IFileSystem& filesystem,
        ICommandRunner& commands,
        IPendingMarkerStore& pending_markers,
        const ISafeDirectoryRootFactory& safe_directories,
        const ITrustedExecutableResolver& hook_executables
    );

    [[nodiscard]] std::unique_ptr<IBackupRunActionHandler> create(
        const BackupRunPlan& plan
    ) override;

  private:
    IBtrfsOperations& btrfs_;
    IFileSystem& filesystem_;
    ICommandRunner& commands_;
    IPendingMarkerStore& pending_markers_;
    const ISafeDirectoryRootFactory& safe_directories_;
    const ITrustedExecutableResolver& hook_executables_;
};

} // namespace btrfsbackup::backup
