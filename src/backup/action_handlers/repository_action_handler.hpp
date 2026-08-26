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
class IPendingMarkerStore;
class ISafeDirectoryRoot;

class RepositoryActionHandler {
  public:
    RepositoryActionHandler(
        IBtrfsOperations& btrfs,
        IFileSystem& filesystem,
        IPendingMarkerStore& pending_markers
    );
    RepositoryActionHandler(
        IBtrfsOperations& btrfs,
        IFileSystem& filesystem,
        IPendingMarkerStore& pending_markers,
        std::unique_ptr<ISafeDirectoryRoot> local_root,
        std::unique_ptr<ISafeDirectoryRoot> target_root
    );
    ~RepositoryActionHandler();

    void handle(const CleanupIncomingAction& action);
    void handle(const VerifyReceivedAction& action);
    void handle(const CommitReceivedAction& action);
    void handle(const CleanupSourceAction& action);

  private:
    IBtrfsOperations& btrfs_;
    IFileSystem& filesystem_;
    IPendingMarkerStore& pending_markers_;
    std::unique_ptr<ISafeDirectoryRoot> local_root_;
    std::unique_ptr<ISafeDirectoryRoot> target_root_;
};

} // namespace btrfsbackup
