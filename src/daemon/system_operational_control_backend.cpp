// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/system_operational_control_backend.hpp>

#include <chrono>
#include <string>
#include <vector>

#include <config/configuration_identity.hpp>
#include <daemon/authorized_operation_command.hpp>
#include <daemon/manager_errors.hpp>

namespace btrfsbackup::daemon {

SystemOperationalControlBackend::SystemOperationalControlBackend(
    btrfsbackup::config::IProfileRepository& profiles,
    btrfsbackup::backup::ICancellationRequestStore& cancellation_requests,
    btrfsbackup::backup::ICommandRunner& commands
) : profiles_(profiles), cancellation_requests_(cancellation_requests), commands_(commands) {
}

OperationalResourceVersion SystemOperationalControlBackend::inspect_profile(const ProfileId& profile_id) const {
    const btrfsbackup::config::LoadedProfile loaded = profiles_.get(profile_id);
    return {
        .generation = loaded.generation,
        .fingerprint = loaded.fingerprint,
    };
}

void SystemOperationalControlBackend::require_profile_version(
    const AuthorizedOperationContext& context
) const {
    const OperationalResourceVersion expected_version{context.generation, context.fingerprint};
    if (inspect_profile(context.profile_id) != expected_version)
        throw ManagerOperationError(ManagerErrorCode::Conflict, "profile changed during authorization");
}

void SystemOperationalControlBackend::run_effect(
    const std::vector<std::string>& command,
    const char* operation
) {
    btrfsbackup::backup::ControlledCommandOptions options;
    options.timeout = std::chrono::minutes(10);
    const btrfsbackup::backup::CommandResult result = commands_.run_controlled(command, options);
    if (result.exit_code == btrfsbackup::config::configuration_changed_exit_code)
        throw ManagerOperationError(ManagerErrorCode::Conflict, "profile changed before operation execution");
    if (result.exit_code != 0 || result.timed_out || result.cancelled)
        throw ManagerOperationError(ManagerErrorCode::TargetUnavailable, std::string(operation) + " failed");
}

void SystemOperationalControlBackend::start_backup(const AuthorizedOperationContext& context) {
    require_profile_version(context);
    run_effect(authorized_backup_command(context), "starting backup");
}

ManagerCancellationOutcome SystemOperationalControlBackend::cancel_backup(
    const RunId& run_id,
    const AuthorizedOperationContext& context
) {
    require_profile_version(context);
    const auto outcome = cancellation_requests_.request_cancel({context.profile_id, run_id});
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

void SystemOperationalControlBackend::validate_target(const AuthorizedOperationContext& context) {
    require_profile_version(context);
    run_effect(authorized_target_validation_command(context), "validating target");
}

void SystemOperationalControlBackend::eject_target(const AuthorizedOperationContext& context) {
    require_profile_version(context);
    run_effect(authorized_target_eject_command(context), "ejecting target");
}

} // namespace btrfsbackup::daemon
