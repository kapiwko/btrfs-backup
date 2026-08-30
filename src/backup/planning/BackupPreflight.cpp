// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/planning/BackupPreflight.hpp>

#include <backup/planning/BackupPreflightValidation.hpp>
#include <core/Errors.hpp>

#include <exception>

namespace btrfsbackup::backup::planning {

BackupPreflight::BackupPreflight(IMountInspector& mount_inspector, ITargetManager& target_manager)
    : mount_inspector_(mount_inspector), target_manager_(target_manager) {
}

BackupPreflightResult BackupPreflight::run(
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
        MountEntry verified_target_mount = validate_backup_mounts(profile, mount_inspector_.inspect());
        if (cancellation.cancellation_requested()) {
            throw OperationCancelledError("backup cancelled during preflight");
        }
        return {
            .target_session = std::move(target_session),
            .verified_target_mount = std::move(verified_target_mount),
        };
    } catch (...) {
        const std::exception_ptr original_error = std::current_exception();
        if (std::optional<TargetCleanupError> cleanup_error = target_session->close()) {
            throw ValidationError(cleanup_error->message);
        }
        std::rethrow_exception(original_error);
    }
}

} // namespace btrfsbackup::backup::planning
