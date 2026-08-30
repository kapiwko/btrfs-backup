// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/action_handlers/RetentionActionHandler.hpp>

#include <utility>

#include <backup/ports/IBtrfsOperations.hpp>
#include <backup/ports/SafeDirectory.hpp>

namespace btrfsbackup::backup {

RetentionActionHandler::RetentionActionHandler(IBtrfsOperations& btrfs) : btrfs_(btrfs) {
}

RetentionActionHandler::RetentionActionHandler(
    IBtrfsOperations& btrfs,
    std::unique_ptr<ISafeDirectoryRoot> local_root,
    std::unique_ptr<ISafeDirectoryRoot> target_root
)
    : btrfs_(btrfs),
      local_root_(std::move(local_root)),
      target_root_(std::move(target_root)) {
}

RetentionActionHandler::~RetentionActionHandler() = default;

void RetentionActionHandler::handle(const ApplyRemoteRetentionAction& action) {
    apply(action.plan, target_root_.get());
}

void RetentionActionHandler::handle(const ApplyLocalRetentionAction& action) {
    apply(action.plan, local_root_.get());
}

void RetentionActionHandler::apply(const RetentionPlan& plan, const ISafeDirectoryRoot* root) {
    for (const SnapshotInfo& snapshot : plan.delete_snapshots) {
        if (root == nullptr) {
            btrfs_.delete_subvolume(snapshot.path);
        } else {
            btrfs_.delete_subvolume_beneath(*root, snapshot.path);
        }
    }
}

} // namespace btrfsbackup::backup
