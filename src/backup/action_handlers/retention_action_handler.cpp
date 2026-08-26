// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/action_handlers/retention_action_handler.hpp>

#include <backup/ports/btrfs_operations.hpp>
#include <platform/linux/safe_directory_root.hpp>

namespace btrfsbackup {

RetentionActionHandler::RetentionActionHandler(IBtrfsOperations& btrfs) : btrfs_(btrfs) {
}

RetentionActionHandler::RetentionActionHandler(
    IBtrfsOperations& btrfs,
    const std::filesystem::path& local_root,
    const std::filesystem::path& target_root
)
    : btrfs_(btrfs),
      local_root_(std::make_unique<SafeDirectoryRoot>(local_root)),
      target_root_(std::make_unique<SafeDirectoryRoot>(target_root)) {
}

RetentionActionHandler::~RetentionActionHandler() = default;

void RetentionActionHandler::handle(const ApplyRemoteRetentionAction& action) {
    apply(action.plan, target_root_.get());
}

void RetentionActionHandler::handle(const ApplyLocalRetentionAction& action) {
    apply(action.plan, local_root_.get());
}

void RetentionActionHandler::apply(const RetentionPlan& plan, const SafeDirectoryRoot* root) {
    for (const SnapshotInfo& snapshot : plan.delete_snapshots) {
        if (root == nullptr) {
            btrfs_.delete_subvolume(snapshot.path);
        } else {
            btrfs_.delete_subvolume_beneath(*root, snapshot.path);
        }
    }
}

} // namespace btrfsbackup
