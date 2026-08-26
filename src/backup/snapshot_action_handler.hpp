// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <memory>

#include <backup/backup_run_actions.hpp>

namespace btrfsbackup {

class IBtrfsOperations;
class IFileSystem;
class SafeDirectoryRoot;

class SnapshotActionHandler {
  public:
    SnapshotActionHandler(IBtrfsOperations& btrfs, IFileSystem& filesystem);
    SnapshotActionHandler(
        IBtrfsOperations& btrfs,
        IFileSystem& filesystem,
        const std::filesystem::path& local_root
    );
    ~SnapshotActionHandler();

    void handle(const CreateSnapshotAction& action);

  private:
    IBtrfsOperations& btrfs_;
    IFileSystem& filesystem_;
    std::unique_ptr<SafeDirectoryRoot> local_root_;
};

} // namespace btrfsbackup
