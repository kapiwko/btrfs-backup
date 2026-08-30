// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

#include <backup/ports/RunLease.hpp>

namespace btrfsbackup::platform::linux::filesystem {

class FileBackupRunLeaseProvider final : public btrfsbackup::backup::IBackupRunLeaseProvider {
  public:
    explicit FileBackupRunLeaseProvider(std::filesystem::path lock_root);

    [[nodiscard]] btrfsbackup::backup::BackupRunLeaseResult try_acquire(const btrfsbackup::config::Profile& profile) override;

  private:
    std::filesystem::path lock_root_;
};

} // namespace btrfsbackup::platform::linux::filesystem
