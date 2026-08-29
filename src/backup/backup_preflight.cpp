// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/backup_preflight.hpp>

#include <backup/backup_preflight_validation.hpp>

namespace btrfsbackup::backup {

BackupPreflight::BackupPreflight(IMountInspector& mount_inspector, ITargetManager& target_manager)
    : mount_inspector_(mount_inspector), target_manager_(target_manager) {
}

std::unique_ptr<IMountedTargetSession> BackupPreflight::run(
    const btrfsbackup::config::Profile& profile,
    TargetMountMode mode
) {
    std::unique_ptr<IMountedTargetSession> target_session = target_manager_.prepare(profile, mode);
    validate_backup_mounts(profile, mount_inspector_.inspect());
    return target_session;
}

} // namespace btrfsbackup::backup
