// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/ports/backup_preflight.hpp>
#include <backup/ports/mount_inspector.hpp>
#include <backup/ports/target_manager.hpp>

namespace btrfsbackup::backup {

class BackupPreflight final : public IBackupPreflight {
  public:
    BackupPreflight(IMountInspector& mount_inspector, ITargetManager& target_manager);

    void run(const btrfsbackup::config::Profile& profile) override;

  private:
    IMountInspector& mount_inspector_;
    ITargetManager& target_manager_;
};

} // namespace btrfsbackup::backup
