// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>

#include <backup/ports/run_lease.hpp>

namespace btrfsbackup {

class FileBackupRunLeaseProvider final : public IBackupRunLeaseProvider {
  public:
    explicit FileBackupRunLeaseProvider(std::filesystem::path lock_root);

    [[nodiscard]] BackupRunLeaseResult try_acquire(const Profile& profile) override;

  private:
    std::filesystem::path lock_root_;
};

} // namespace btrfsbackup
