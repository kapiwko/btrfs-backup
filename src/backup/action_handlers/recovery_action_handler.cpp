// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/action_handlers/recovery_action_handler.hpp>

#include <utility>

#include <backup/ports/btrfs_operations.hpp>
#include <backup/ports/safe_directory.hpp>
#include <state/run_state.hpp>

namespace btrfsbackup {

RecoveryActionHandler::RecoveryActionHandler(IBtrfsOperations& btrfs) : btrfs_(btrfs) {
}

RecoveryActionHandler::RecoveryActionHandler(
    IBtrfsOperations& btrfs,
    std::unique_ptr<ISafeDirectoryRoot> local_root,
    std::unique_ptr<ISafeDirectoryRoot> target_root
)
    : btrfs_(btrfs),
      local_root_(std::move(local_root)),
      target_root_(std::move(target_root)) {
}

RecoveryActionHandler::~RecoveryActionHandler() = default;

void RecoveryActionHandler::handle(const RecoverPendingAction& action) {
    if (action.recovery.delete_remote_snapshot) {
        if (target_root_ == nullptr) {
            btrfs_.delete_subvolume(action.recovery.remote_snapshot_path);
        } else {
            btrfs_.delete_subvolume_beneath(*target_root_, action.recovery.remote_snapshot_path);
        }
    }
    if (action.recovery.delete_local_snapshot) {
        if (local_root_ == nullptr) {
            btrfs_.delete_subvolume(action.recovery.local_snapshot_path);
        } else {
            btrfs_.delete_subvolume_beneath(*local_root_, action.recovery.local_snapshot_path);
        }
    }
    if (action.recovery.clear_marker) {
        clear_pending_marker(action.recovery.marker_path, action.recovery.marker_path.parent_path());
    }
}

} // namespace btrfsbackup
