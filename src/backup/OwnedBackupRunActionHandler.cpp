// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/OwnedBackupRunActionHandler.hpp>

#include <utility>

namespace btrfsbackup::backup {

OwnedBackupRunActionHandler::OwnedBackupRunActionHandler(
    IBtrfsOperations& btrfs,
    IFileSystem& filesystem,
    ICommandRunner& commands,
    IPendingMarkerStore& pending_markers,
    const ISafeDirectoryRootFactory& safe_directories,
    const ITrustedExecutableResolver& hook_executables,
    const BackupRunPlan& plan
)
    : snapshots_(btrfs, filesystem, pending_markers, safe_directories.open("/")),
      recovery_(
          btrfs,
          pending_markers,
          safe_directories.open("/"),
          safe_directories.open(plan.target_mount_point)
      ),
      retention_(
          btrfs,
          safe_directories.open("/"),
          safe_directories.open(plan.target_mount_point)
      ),
      hooks_(commands, hook_executables),
      local_repository_root_(safe_directories.open("/")),
      target_repository_root_(safe_directories.open(plan.target_mount_point)),
      repository_(
          btrfs,
          pending_markers,
          *local_repository_root_,
          *target_repository_root_
      ),
      dispatcher_(snapshots_, recovery_, retention_, hooks_, repository_) {
}

void OwnedBackupRunActionHandler::handle(
    const BackupRunAction& action,
    const BackupRunPlan& plan,
    CancellationToken& cancellation
) {
    dispatcher_.handle(action, plan, cancellation);
}

} // namespace btrfsbackup::backup
