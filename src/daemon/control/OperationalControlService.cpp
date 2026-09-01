// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/OperationalControlService.hpp>

#include <atomic>
#include <chrono>
#include <string>
#include <utility>

#include <core/ManagerProtocol.hpp>
#include <daemon/dbus/ManagerErrors.hpp>

namespace btrfsbackup::daemon::control {

namespace {

OperationId generate_operation_id() {
    static std::atomic<unsigned long long> sequence{0};
    const auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
                               std::chrono::system_clock::now().time_since_epoch()
    )
                               .count();
    return OperationId{"op-" + std::to_string(timestamp) + "-" + std::to_string(sequence.fetch_add(1))};
}

} // namespace

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
    case ManagerAuthorizationAction::ManageProfileConfiguration:
        return "io.github.btrfsbackup.manage-profile-configuration";
    case ManagerAuthorizationAction::DeleteProfileConfiguration:
        return "io.github.btrfsbackup.delete-profile-configuration";
    case ManagerAuthorizationAction::SetProfileEnabled:
        return "io.github.btrfsbackup.set-profile-enabled";
    case ManagerAuthorizationAction::OpenBrowseSession:
        return "io.github.btrfsbackup.open-browse-session";
    case ManagerAuthorizationAction::ManageTargetCredentials:
        return "io.github.btrfsbackup.manage-target-credentials";
    case ManagerAuthorizationAction::PrepareBackupDevice:
        return "io.github.btrfsbackup.prepare-backup-device";
    }
    return "io.github.btrfsbackup.invalid-action";
}

OperationalControlService::OperationalControlService(
    IManagerAuthorizer& authorizer,
    IOperationalControlBackend& backend,
    OperationIdGenerator operation_ids
) : authorizer_(authorizer),
    backend_(backend),
    operation_ids_(operation_ids ? std::move(operation_ids) : OperationIdGenerator{generate_operation_id}) {
}

void OperationalControlService::require_authorized(
    const std::string& caller_bus_name,
    ManagerAuthorizationAction action
) {
    if (caller_bus_name.empty() || !authorizer_.authorize(caller_bus_name, action) ||
        !authorizer_.caller_is_active(caller_bus_name))
        throw dbus::ManagerOperationError(
            dbus::ManagerErrorCode::NotAuthorized,
            "manager operation was not authorized"
        );
}

AuthorizedOperationContext OperationalControlService::authorized_context(
    const ProfileId& profile_id,
    const OperationalResourceVersion& version
) {
    return {
        .profile_id = profile_id,
        .generation = version.generation,
        .fingerprint = version.fingerprint,
        .operation_id = operation_ids_(),
    };
}

OperationResult OperationalControlService::start_backup(
    const std::string& caller_bus_name,
    const std::string& profile_id
) {
    const ProfileId validated_profile(profile_id);
    const OperationalResourceVersion version = backend_.inspect_profile(validated_profile);
    require_authorized(caller_bus_name, ManagerAuthorizationAction::StartBackup);
    const AuthorizedOperationContext context = authorized_context(validated_profile, version);
    backend_.start_backup(context);
    return {
        .operation = manager_protocol::feature::start_backup,
        .operation_id = std::string(context.operation_id.value()),
        .profile_id = profile_id,
        .run_id = {},
        .accepted = true,
    };
}

OperationResult OperationalControlService::cancel_backup(
    const std::string& caller_bus_name,
    const std::string& profile_id,
    const std::string& run_id
) {
    const ProfileId validated_profile(profile_id);
    const RunId validated_run(run_id);
    const OperationalResourceVersion version = backend_.inspect_profile(validated_profile);
    require_authorized(caller_bus_name, ManagerAuthorizationAction::CancelBackup);
    const AuthorizedOperationContext context = authorized_context(validated_profile, version);
    switch (backend_.cancel_backup(validated_run, context)) {
    case ManagerCancellationOutcome::Accepted:
        return {
            .operation = manager_protocol::feature::cancel_backup,
            .operation_id = std::string(context.operation_id.value()),
            .profile_id = profile_id,
            .run_id = run_id,
        };
    case ManagerCancellationOutcome::StaleRun:
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotFound, "backup run is no longer active");
    case ManagerCancellationOutcome::RunMismatch:
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::RunMismatch, "a different backup run is active");
    }
    throw dbus::ManagerOperationError(dbus::ManagerErrorCode::InternalError, "unknown cancellation outcome");
}

OperationResult OperationalControlService::validate_target(
    const std::string& caller_bus_name,
    const std::string& profile_id
) {
    const ProfileId validated_profile(profile_id);
    const OperationalResourceVersion version = backend_.inspect_profile(validated_profile);
    require_authorized(caller_bus_name, ManagerAuthorizationAction::ValidateTarget);
    const AuthorizedOperationContext context = authorized_context(validated_profile, version);
    backend_.validate_target(context);
    return {
        .operation = manager_protocol::feature::validate_target,
        .operation_id = std::string(context.operation_id.value()),
        .profile_id = profile_id,
        .run_id = {},
        .accepted = true,
    };
}

OperationResult OperationalControlService::eject_target(
    const std::string& caller_bus_name,
    const std::string& profile_id
) {
    const ProfileId validated_profile(profile_id);
    const OperationalResourceVersion version = backend_.inspect_profile(validated_profile);
    require_authorized(caller_bus_name, ManagerAuthorizationAction::EjectTarget);
    const AuthorizedOperationContext context = authorized_context(validated_profile, version);
    backend_.eject_target(context);
    return {
        .operation = manager_protocol::feature::eject_target,
        .operation_id = std::string(context.operation_id.value()),
        .profile_id = profile_id,
        .run_id = {},
        .accepted = true,
    };
}

} // namespace btrfsbackup::daemon::control
