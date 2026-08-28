// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <memory>

#include <backup/model/backup_run_actions.hpp>

namespace btrfsbackup::backup {

class IBtrfsOperations;
class ISafeDirectoryRoot;

class RetentionActionHandler {
  public:
    explicit RetentionActionHandler(IBtrfsOperations& btrfs);
    RetentionActionHandler(
        IBtrfsOperations& btrfs,
        std::unique_ptr<ISafeDirectoryRoot> local_root,
        std::unique_ptr<ISafeDirectoryRoot> target_root
    );
    ~RetentionActionHandler();

    void handle(const ApplyRemoteRetentionAction& action);
    void handle(const ApplyLocalRetentionAction& action);

  private:
    void apply(const RetentionPlan& plan, const ISafeDirectoryRoot* root);

    IBtrfsOperations& btrfs_;
    std::unique_ptr<ISafeDirectoryRoot> local_root_;
    std::unique_ptr<ISafeDirectoryRoot> target_root_;
};

} // namespace btrfsbackup::backup
