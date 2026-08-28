// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <memory>

#include <backup/model/backup_run_actions.hpp>

namespace btrfsbackup::backup {

class IFileSystem;
class ISafeDirectoryRoot;

class TransferActionHandler {
  public:
    explicit TransferActionHandler(IFileSystem& filesystem);
    TransferActionHandler(IFileSystem& filesystem, std::unique_ptr<ISafeDirectoryRoot> target_root);
    ~TransferActionHandler();

    void handle(const SendReceiveAction& action);

  private:
    IFileSystem& filesystem_;
    std::unique_ptr<ISafeDirectoryRoot> target_root_;
};

} // namespace btrfsbackup::backup
