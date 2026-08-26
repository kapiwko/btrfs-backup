// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <memory>

#include <backup/model/backup_run_actions.hpp>

namespace btrfsbackup {

class IBtrfsOperations;
class IDurableFileOperations;
class ISafeDirectoryRoot;

class RecoveryActionHandler {
  public:
    RecoveryActionHandler(IBtrfsOperations& btrfs, IDurableFileOperations& durable_files);
    RecoveryActionHandler(
        IBtrfsOperations& btrfs,
        IDurableFileOperations& durable_files,
        std::unique_ptr<ISafeDirectoryRoot> local_root,
        std::unique_ptr<ISafeDirectoryRoot> target_root
    );
    ~RecoveryActionHandler();

    void handle(const RecoverPendingAction& action);

  private:
    IBtrfsOperations& btrfs_;
    IDurableFileOperations& durable_files_;
    std::unique_ptr<ISafeDirectoryRoot> local_root_;
    std::unique_ptr<ISafeDirectoryRoot> target_root_;
};

} // namespace btrfsbackup
