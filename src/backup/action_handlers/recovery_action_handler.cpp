// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/action_handlers/recovery_action_handler.hpp>

#include <utility>

#include <backup/ports/btrfs_operations.hpp>
#include <backup/ports/pending_marker_store.hpp>
#include <backup/ports/safe_directory.hpp>

namespace btrfsbackup::backup {

RecoveryActionHandler::RecoveryActionHandler(IBtrfsOperations& btrfs, IPendingMarkerStore& pending_markers)
    : btrfs_(btrfs), pending_markers_(pending_markers) {
}

RecoveryActionHandler::RecoveryActionHandler(
    IBtrfsOperations& btrfs,
    IPendingMarkerStore& pending_markers,
    std::unique_ptr<ISafeDirectoryRoot> local_root,
    std::unique_ptr<ISafeDirectoryRoot> target_root
)
    : btrfs_(btrfs),
      pending_markers_(pending_markers),
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
        pending_markers_.clear(action.recovery.marker_path);
    }
}

} // namespace btrfsbackup::backup
