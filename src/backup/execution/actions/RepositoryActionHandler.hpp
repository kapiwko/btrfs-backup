// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/model/BackupRunActions.hpp>

namespace btrfsbackup::backup {
class IBtrfsOperations;
class IPendingMarkerStore;
class ISafeDirectoryRoot;
}

namespace btrfsbackup::backup::execution {

class RepositoryActionHandler {
  public:
    RepositoryActionHandler(
        IBtrfsOperations& btrfs,
        IPendingMarkerStore& pending_markers,
        ISafeDirectoryRoot& local_root,
        ISafeDirectoryRoot& target_root
    );

    void handle(const CleanupIncomingAction& action);
    void handle(const VerifyReceivedAction& action);
    void handle(const CommitReceivedAction& action);
    void handle(const CleanupSourceAction& action);

  private:
    IBtrfsOperations& btrfs_;
    IPendingMarkerStore& pending_markers_;
    ISafeDirectoryRoot& local_root_;
    ISafeDirectoryRoot& target_root_;
};

} // namespace btrfsbackup::backup::execution
