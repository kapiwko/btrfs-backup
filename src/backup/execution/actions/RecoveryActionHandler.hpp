// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <memory>

#include <backup/model/BackupRunActions.hpp>

namespace btrfsbackup::backup {
class IBtrfsOperations;
class IPendingMarkerStore;
class ISafeDirectoryRoot;
}

namespace btrfsbackup::backup::execution {

class RecoveryActionHandler {
  public:
    RecoveryActionHandler(IBtrfsOperations& btrfs, IPendingMarkerStore& pending_markers);
    RecoveryActionHandler(
        IBtrfsOperations& btrfs,
        IPendingMarkerStore& pending_markers,
        std::unique_ptr<ISafeDirectoryRoot> local_root,
        std::unique_ptr<ISafeDirectoryRoot> target_root
    );
    ~RecoveryActionHandler() noexcept;

    void handle(const RecoverPendingAction& action);

  private:
    IBtrfsOperations& btrfs_;
    IPendingMarkerStore& pending_markers_;
    std::unique_ptr<ISafeDirectoryRoot> local_root_;
    std::unique_ptr<ISafeDirectoryRoot> target_root_;
};

} // namespace btrfsbackup::backup::execution
