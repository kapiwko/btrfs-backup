// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/backup_preflight.hpp>

#include <backup/backup_preflight_validation.hpp>

namespace btrfsbackup::backup {

BackupPreflight::BackupPreflight(IMountInspector& mount_inspector, ITargetManager& target_manager)
    : mount_inspector_(mount_inspector), target_manager_(target_manager) {
}

void BackupPreflight::run(const btrfsbackup::config::Profile& profile) {
    target_manager_.ensure_mounted(profile);
    validate_backup_mounts(profile, mount_inspector_.inspect());
}

} // namespace btrfsbackup::backup
