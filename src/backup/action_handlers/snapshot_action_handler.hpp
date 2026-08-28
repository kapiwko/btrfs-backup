// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <memory>

#include <backup/model/backup_run_actions.hpp>

namespace btrfsbackup::backup {

class IBtrfsOperations;
class IFileSystem;
class IPendingMarkerStore;
class ISafeDirectoryRoot;

class SnapshotActionHandler {
  public:
    SnapshotActionHandler(
        IBtrfsOperations& btrfs,
        IFileSystem& filesystem,
        IPendingMarkerStore& pending_markers
    );
    SnapshotActionHandler(
        IBtrfsOperations& btrfs,
        IFileSystem& filesystem,
        IPendingMarkerStore& pending_markers,
        std::unique_ptr<ISafeDirectoryRoot> local_root
    );
    ~SnapshotActionHandler();

    void handle(const CreateSnapshotAction& action);

  private:
    IBtrfsOperations& btrfs_;
    IFileSystem& filesystem_;
    IPendingMarkerStore& pending_markers_;
    std::unique_ptr<ISafeDirectoryRoot> local_root_;
};

} // namespace btrfsbackup::backup
