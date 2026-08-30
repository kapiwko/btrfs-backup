// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <memory>

#include <backup/model/BackupRunActions.hpp>

namespace btrfsbackup::backup {

class IBtrfsOperations;
class IFileSystem;
class IPendingMarkerStore;
class ISafeDirectoryRoot;
class IClock;

class SnapshotActionHandler {
  public:
    SnapshotActionHandler(
        IBtrfsOperations& btrfs,
        IFileSystem& filesystem,
        IPendingMarkerStore& pending_markers,
        IClock& clock
    );
    SnapshotActionHandler(
        IBtrfsOperations& btrfs,
        IFileSystem& filesystem,
        IPendingMarkerStore& pending_markers,
        IClock& clock,
        std::unique_ptr<ISafeDirectoryRoot> local_root
    );
    ~SnapshotActionHandler() noexcept;

    void handle(const CreateSnapshotAction& action);

  private:
    IBtrfsOperations& btrfs_;
    IFileSystem& filesystem_;
    IPendingMarkerStore& pending_markers_;
    IClock& clock_;
    std::unique_ptr<ISafeDirectoryRoot> local_root_;
};

} // namespace btrfsbackup::backup
