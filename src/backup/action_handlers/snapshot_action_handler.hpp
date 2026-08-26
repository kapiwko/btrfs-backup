// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <memory>

#include <backup/model/backup_run_actions.hpp>

namespace btrfsbackup {

class IBtrfsOperations;
class IFileSystem;
class ISafeDirectoryRoot;

class SnapshotActionHandler {
  public:
    SnapshotActionHandler(IBtrfsOperations& btrfs, IFileSystem& filesystem);
    SnapshotActionHandler(
        IBtrfsOperations& btrfs,
        IFileSystem& filesystem,
        std::unique_ptr<ISafeDirectoryRoot> local_root
    );
    ~SnapshotActionHandler();

    void handle(const CreateSnapshotAction& action);

  private:
    IBtrfsOperations& btrfs_;
    IFileSystem& filesystem_;
    std::unique_ptr<ISafeDirectoryRoot> local_root_;
};

} // namespace btrfsbackup
