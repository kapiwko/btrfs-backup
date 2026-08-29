// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/backup_preflight.hpp>

#include <backup/backup_preflight_validation.hpp>
#include <core/errors.hpp>

namespace btrfsbackup::backup {

BackupPreflight::BackupPreflight(IMountInspector& mount_inspector, ITargetManager& target_manager)
    : mount_inspector_(mount_inspector), target_manager_(target_manager) {
}

std::unique_ptr<IMountedTargetSession> BackupPreflight::run(
    const btrfsbackup::config::Profile& profile,
    TargetMountMode mode,
    CancellationToken& cancellation
) {
    if (cancellation.cancellation_requested()) {
        throw OperationCancelledError("backup cancelled during preflight");
    }
    std::unique_ptr<IMountedTargetSession> target_session = target_manager_.prepare(profile, mode);
    if (cancellation.cancellation_requested()) {
        throw OperationCancelledError("backup cancelled during preflight");
    }
    validate_backup_mounts(profile, mount_inspector_.inspect());
    if (cancellation.cancellation_requested()) {
        throw OperationCancelledError("backup cancelled during preflight");
    }
    return target_session;
}

} // namespace btrfsbackup::backup
