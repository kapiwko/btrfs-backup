// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/ProfileAdministrationService.hpp>

#include <daemon/dbus/ManagerErrors.hpp>

namespace btrfsbackup::daemon::control {

ProfileAdministrationService::ProfileAdministrationService(
    IManagerAuthorizer& authorizer,
    IProfileAdministrationBackend& backend
) : authorizer_(authorizer), backend_(backend) {
}

void ProfileAdministrationService::require_authorized(
    const std::string& caller,
    ManagerAuthorizationAction action
) {
    if (caller.empty() || !authorizer_.authorize(caller, action) || !authorizer_.caller_is_active(caller)) {
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotAuthorized, "profile administration was not authorized");
    }
}

EditableProfile ProfileAdministrationService::expected_profile(
    const ProfileId& profile_id,
    const std::string& generation,
    const std::string& fingerprint
) {
    return {
        .profile_id = std::string(profile_id.value()),
        .generation = generation,
        .fingerprint = fingerprint,
        .document = {},
    };
}

void ProfileAdministrationService::require_current(
    const EditableProfile& current,
    const EditableProfile& expected
) {
    if (current.profile_id != expected.profile_id || current.generation != expected.generation ||
        current.fingerprint != expected.fingerprint) {
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Conflict, "profile configuration changed");
    }
}

void ProfileAdministrationService::require_current(
    const std::optional<EditableProfile>& current,
    const EditableProfile& expected
) {
    if (!current.has_value()) {
        if (!expected.generation.empty() || !expected.fingerprint.empty())
            throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Conflict, "profile configuration changed");
        return;
    }
    require_current(*current, expected);
}

EditableProfile ProfileAdministrationService::get_profile_for_editing(
    const std::string& caller,
    const std::string& profile_id
) {
    const ProfileId id(profile_id);
    require_authorized(caller, ManagerAuthorizationAction::ReadProfileConfiguration);
    const auto profile = backend_.find_profile(id);
    if (!profile.has_value())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::NotFound, "profile does not exist");
    return *profile;
}

ProfileDraftResult ProfileAdministrationService::validate_profile_draft(
    const std::string& caller,
    const std::string& profile_id,
    const std::string& expected_generation,
    const std::string& expected_fingerprint,
    const std::string& document
) {
    const ProfileId id(profile_id);
    ProfileDraftResult draft = backend_.validate_draft(id, document);
    const EditableProfile expected = expected_profile(id, expected_generation, expected_fingerprint);
    require_current(backend_.find_profile(id), expected);
    require_authorized(caller, ManagerAuthorizationAction::ReadProfileConfiguration);
    require_current(backend_.find_profile(id), expected);
    draft.generation = expected_generation;
    draft.fingerprint = expected_fingerprint;
    return draft;
}

ProfileDraftResult ProfileAdministrationService::save_profile(
    const std::string& caller,
    const std::string& profile_id,
    const std::string& expected_generation,
    const std::string& expected_fingerprint,
    const std::string& document
) {
    const ProfileId id(profile_id);
    const ProfileDraftResult draft = backend_.validate_draft(id, document);
    const EditableProfile expected = expected_profile(id, expected_generation, expected_fingerprint);
    require_current(backend_.find_profile(id), expected);
    require_authorized(caller, ManagerAuthorizationAction::SaveProfileConfiguration);
    require_current(backend_.find_profile(id), expected);
    return backend_.save_profile(expected, draft, false);
}

ProfileDraftResult ProfileAdministrationService::save_profile_hooks(
    const std::string& caller,
    const std::string& profile_id,
    const std::string& expected_generation,
    const std::string& expected_fingerprint,
    const std::string& document
) {
    const ProfileId id(profile_id);
    const ProfileDraftResult draft = backend_.validate_draft(id, document);
    const EditableProfile expected = expected_profile(id, expected_generation, expected_fingerprint);
    require_current(backend_.find_profile(id), expected);
    require_authorized(caller, ManagerAuthorizationAction::SaveProfileHooks);
    require_authorized(caller, ManagerAuthorizationAction::SaveProfileConfiguration);
    require_current(backend_.find_profile(id), expected);
    return backend_.save_profile(expected, draft, true);
}

void ProfileAdministrationService::delete_profile(
    const std::string& caller,
    const std::string& profile_id,
    const std::string& expected_generation,
    const std::string& expected_fingerprint
) {
    const ProfileId id(profile_id);
    const EditableProfile expected = expected_profile(id, expected_generation, expected_fingerprint);
    require_current(backend_.find_profile(id), expected);
    require_authorized(caller, ManagerAuthorizationAction::DeleteProfileConfiguration);
    require_current(backend_.find_profile(id), expected);
    backend_.delete_profile(expected);
}

} // namespace btrfsbackup::daemon::control
