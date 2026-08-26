// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <memory>

#include <backup/model/backup_run_actions.hpp>

namespace btrfsbackup {

class IFileSystem;
class SafeDirectoryRoot;

class TransferActionHandler {
  public:
    explicit TransferActionHandler(IFileSystem& filesystem);
    TransferActionHandler(IFileSystem& filesystem, const std::filesystem::path& target_root);
    ~TransferActionHandler();

    void handle(const SendReceiveAction& action);

  private:
    IFileSystem& filesystem_;
    std::unique_ptr<SafeDirectoryRoot> target_root_;
};

} // namespace btrfsbackup
