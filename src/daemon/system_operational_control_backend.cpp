// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/system_operational_control_backend.hpp>

#include <chrono>
#include <string>
#include <vector>

#include <daemon/manager_errors.hpp>

namespace btrfsbackup::daemon {

SystemOperationalControlBackend::SystemOperationalControlBackend(
    btrfsbackup::config::IProfileRepository& profiles,
    btrfsbackup::backup::ICancellationRequestStore& cancellation_requests,
    btrfsbackup::backup::ICommandRunner& commands
) : profiles_(profiles), cancellation_requests_(cancellation_requests), commands_(commands) {
}

void SystemOperationalControlBackend::require_profile(const ProfileId& profile_id) const {
    (void)profiles_.get(profile_id);
}

void SystemOperationalControlBackend::run_effect(
    const std::vector<std::string>& command,
    const char* operation
) {
    btrfsbackup::backup::ControlledCommandOptions options;
    options.timeout = std::chrono::minutes(10);
    const btrfsbackup::backup::CommandResult result = commands_.run_controlled(command, options);
    if (result.exit_code != 0 || result.timed_out || result.cancelled)
        throw ManagerOperationError(ManagerErrorCode::TargetUnavailable, std::string(operation) + " failed");
}

void SystemOperationalControlBackend::start_backup(const ProfileId& profile_id) {
    require_profile(profile_id);
    run_effect(
        {"systemctl", "--no-block", "start", "btrfs-backup@" + std::string(profile_id.value()) + ".service"},
        "starting backup"
    );
}

ManagerCancellationOutcome SystemOperationalControlBackend::cancel_backup(
    const ProfileId& profile_id,
    const RunId& run_id
) {
    require_profile(profile_id);
    const auto outcome = cancellation_requests_.request_cancel({profile_id, run_id});
    switch (outcome) {
    case btrfsbackup::backup::CancellationRequestOutcome::Accepted:
        return ManagerCancellationOutcome::Accepted;
    case btrfsbackup::backup::CancellationRequestOutcome::StaleRun:
        return ManagerCancellationOutcome::StaleRun;
    case btrfsbackup::backup::CancellationRequestOutcome::RunMismatch:
        return ManagerCancellationOutcome::RunMismatch;
    }
    throw ManagerOperationError(ManagerErrorCode::InternalError, "unknown cancellation outcome");
}

void SystemOperationalControlBackend::validate_target(const ProfileId& profile_id) {
    require_profile(profile_id);
    const std::string id(profile_id.value());
    run_effect(
        {
            "systemd-run",
            "--quiet",
            "--wait",
            "--collect",
            "--unit=btrfs-backup-validate@" + id + ".service",
            "/usr/bin/btrfs-backup",
            "--profile",
            id,
            "--validate",
            "--no-eject",
        },
        "validating target"
    );
}

void SystemOperationalControlBackend::eject_target(const ProfileId& profile_id) {
    require_profile(profile_id);
    run_effect(
        {"systemctl", "start", "btrfs-backup-eject@" + std::string(profile_id.value()) + ".service"},
        "ejecting target"
    );
}

} // namespace btrfsbackup::daemon
