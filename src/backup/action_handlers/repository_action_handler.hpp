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
class SafeDirectoryRoot;

class RepositoryActionHandler {
  public:
    RepositoryActionHandler(IBtrfsOperations& btrfs, IFileSystem& filesystem);
    RepositoryActionHandler(
        IBtrfsOperations& btrfs,
        IFileSystem& filesystem,
        const std::filesystem::path& local_root,
        const std::filesystem::path& target_root
    );
    ~RepositoryActionHandler();

    void handle(const CleanupIncomingAction& action);
    void handle(const VerifyReceivedAction& action);
    void handle(const CommitReceivedAction& action);
    void handle(const CleanupSourceAction& action);

  private:
    IBtrfsOperations& btrfs_;
    IFileSystem& filesystem_;
    std::unique_ptr<SafeDirectoryRoot> local_root_;
    std::unique_ptr<SafeDirectoryRoot> target_root_;
};

} // namespace btrfsbackup
