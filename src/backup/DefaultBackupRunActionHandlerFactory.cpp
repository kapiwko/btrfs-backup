// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/DefaultBackupRunActionHandlerFactory.hpp>

#include <memory>

#include <backup/action_handlers/BackupRunActionHandler.hpp>
#include <backup/action_handlers/HookActionHandler.hpp>
#include <backup/action_handlers/RecoveryActionHandler.hpp>
#include <backup/action_handlers/RepositoryActionHandler.hpp>
#include <backup/action_handlers/RetentionActionHandler.hpp>
#include <backup/action_handlers/SnapshotActionHandler.hpp>
#include <backup/ports/SafeDirectory.hpp>

namespace btrfsbackup::backup {
namespace {

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

    void handle(
        const BackupRunAction& action,
        const BackupRunPlan& plan,
        CancellationToken& cancellation
    ) override {
        dispatcher_.handle(action, plan, cancellation);
    }

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

} // namespace

DefaultBackupRunActionHandlerFactory::DefaultBackupRunActionHandlerFactory(
    IBtrfsOperations& btrfs,
    IFileSystem& filesystem,
    ICommandRunner& commands,
    IPendingMarkerStore& pending_markers,
    const ISafeDirectoryRootFactory& safe_directories,
    const ITrustedExecutableResolver& hook_executables
)
    : btrfs_(btrfs),
      filesystem_(filesystem),
      commands_(commands),
      pending_markers_(pending_markers),
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
        safe_directories_,
        hook_executables_,
        plan
    );
}

} // namespace btrfsbackup::backup
