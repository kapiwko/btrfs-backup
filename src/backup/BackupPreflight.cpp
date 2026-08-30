// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/BackupPreflight.hpp>

#include <backup/BackupPreflightValidation.hpp>
#include <core/Errors.hpp>

#include <exception>

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
    try {
        if (cancellation.cancellation_requested()) {
            throw OperationCancelledError("backup cancelled during preflight");
        }
        validate_backup_mounts(profile, mount_inspector_.inspect());
        if (cancellation.cancellation_requested()) {
            throw OperationCancelledError("backup cancelled during preflight");
        }
    } catch (...) {
        const std::exception_ptr original_error = std::current_exception();
        if (std::optional<TargetCleanupError> cleanup_error = target_session->close()) {
            throw ValidationError(cleanup_error->message);
        }
        std::rethrow_exception(original_error);
    }
    return target_session;
}

} // namespace btrfsbackup::backup
