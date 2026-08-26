// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <memory>

#include <backup/backup_run_actions.hpp>

namespace btrfsbackup {

class IBtrfsOperations;
class SafeDirectoryRoot;

class RecoveryActionHandler {
  public:
    explicit RecoveryActionHandler(IBtrfsOperations& btrfs);
    RecoveryActionHandler(
        IBtrfsOperations& btrfs,
        const std::filesystem::path& local_root,
        const std::filesystem::path& target_root
    );
    ~RecoveryActionHandler();

    void handle(const RecoverPendingAction& action);

  private:
    IBtrfsOperations& btrfs_;
    std::unique_ptr<SafeDirectoryRoot> local_root_;
    std::unique_ptr<SafeDirectoryRoot> target_root_;
};

} // namespace btrfsbackup
