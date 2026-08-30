// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/DefaultBackupRunActionHandlerFactory.hpp>

#include <memory>

#include <backup/OwnedBackupRunActionHandler.hpp>

namespace btrfsbackup::backup {

DefaultBackupRunActionHandlerFactory::DefaultBackupRunActionHandlerFactory(
    IBtrfsOperations& btrfs,
    IFileSystem& filesystem,
    ICommandRunner& commands,
    IPendingMarkerStore& pending_markers,
    IClock& clock,
    const ISafeDirectoryRootFactory& safe_directories,
    const ITrustedExecutableResolver& hook_executables
)
    : btrfs_(btrfs),
      filesystem_(filesystem),
      commands_(commands),
      pending_markers_(pending_markers),
      clock_(clock),
      safe_directories_(safe_directories),
      hook_executables_(hook_executables) {
}

std::unique_ptr<IBackupRunActionHandler> DefaultBackupRunActionHandlerFactory::create(
    const BackupRunPlan& plan
) {
    return std::make_unique<OwnedBackupRunActionHandler>(
        btrfs_,
        filesystem_,
        commands_,
        pending_markers_,
        clock_,
        safe_directories_,
        hook_executables_,
        plan
    );
}

} // namespace btrfsbackup::backup
