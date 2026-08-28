// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/operational_control_service.hpp>

#include <string>

#include <daemon/manager_errors.hpp>

namespace btrfsbackup::daemon {

const char* manager_authorization_action_id(ManagerAuthorizationAction action) noexcept {
    switch (action) {
    case ManagerAuthorizationAction::StartBackup:
        return "io.github.btrfsbackup.start-backup";
    case ManagerAuthorizationAction::CancelBackup:
        return "io.github.btrfsbackup.cancel-backup";
    case ManagerAuthorizationAction::ValidateTarget:
        return "io.github.btrfsbackup.validate-target";
    case ManagerAuthorizationAction::EjectTarget:
        return "io.github.btrfsbackup.eject-target";
    }
    return "io.github.btrfsbackup.invalid-action";
}

OperationalControlService::OperationalControlService(
    IManagerAuthorizer& authorizer,
    IOperationalControlBackend& backend
) : authorizer_(authorizer), backend_(backend) {
}

void OperationalControlService::require_authorized(
    const std::string& caller_bus_name,
    ManagerAuthorizationAction action
) {
    if (caller_bus_name.empty() || !authorizer_.authorize(caller_bus_name, action))
        throw ManagerOperationError(ManagerErrorCode::NotAuthorized, "manager operation was not authorized");
}

OperationResult OperationalControlService::start_backup(
    const std::string& caller_bus_name,
    const std::string& profile_id
) {
    const ProfileId validated_profile(profile_id);
    require_authorized(caller_bus_name, ManagerAuthorizationAction::StartBackup);
    backend_.start_backup(validated_profile);
    return {.operation = "start-backup", .profile_id = profile_id, .run_id = {}, .accepted = true};
}

OperationResult OperationalControlService::cancel_backup(
    const std::string& caller_bus_name,
    const std::string& profile_id,
    const std::string& run_id
) {
    const ProfileId validated_profile(profile_id);
    const RunId validated_run(run_id);
    require_authorized(caller_bus_name, ManagerAuthorizationAction::CancelBackup);
    switch (backend_.cancel_backup(validated_profile, validated_run)) {
    case ManagerCancellationOutcome::Accepted:
        return {.operation = "cancel-backup", .profile_id = profile_id, .run_id = run_id};
    case ManagerCancellationOutcome::StaleRun:
        throw ManagerOperationError(ManagerErrorCode::NotFound, "backup run is no longer active");
    case ManagerCancellationOutcome::RunMismatch:
        throw ManagerOperationError(ManagerErrorCode::RunMismatch, "a different backup run is active");
    }
    throw ManagerOperationError(ManagerErrorCode::InternalError, "unknown cancellation outcome");
}

OperationResult OperationalControlService::validate_target(
    const std::string& caller_bus_name,
    const std::string& profile_id
) {
    const ProfileId validated_profile(profile_id);
    require_authorized(caller_bus_name, ManagerAuthorizationAction::ValidateTarget);
    backend_.validate_target(validated_profile);
    return {.operation = "validate-target", .profile_id = profile_id, .run_id = {}, .accepted = true};
}

OperationResult OperationalControlService::eject_target(
    const std::string& caller_bus_name,
    const std::string& profile_id
) {
    const ProfileId validated_profile(profile_id);
    require_authorized(caller_bus_name, ManagerAuthorizationAction::EjectTarget);
    backend_.eject_target(validated_profile);
    return {.operation = "eject-target", .profile_id = profile_id, .run_id = {}, .accepted = true};
}

} // namespace btrfsbackup::daemon
