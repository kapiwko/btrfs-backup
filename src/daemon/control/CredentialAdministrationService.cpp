// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/CredentialAdministrationService.hpp>

#include <algorithm>
#include <cctype>

#include <daemon/dbus/ManagerErrors.hpp>

namespace btrfsbackup::daemon::control {

namespace {

std::string trim(std::string value) {
    const auto whitespace = [](unsigned char character) { return std::isspace(character) != 0; };
    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), whitespace));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), whitespace).base(), value.end());
    return value;
}

} // namespace

CredentialAdministrationService::CredentialAdministrationService(
    IManagerAuthorizer& authorizer,
    ICredentialAdministrationBackend& backend
)
    : authorizer_(authorizer), backend_(backend) {
}

std::vector<TargetCredential> CredentialAdministrationService::list_credentials(
    const std::string& profile_id
) const {
    return backend_.list_credentials(ProfileId{profile_id});
}

void CredentialAdministrationService::authorize(const std::string& caller) const {
    if (caller.empty() ||
        !authorizer_.authorize(caller, ManagerAuthorizationAction::ManageTargetCredentials) ||
        !authorizer_.caller_is_active(caller)) {
        throw dbus::ManagerOperationError(
            dbus::ManagerErrorCode::NotAuthorized,
            "operation is not authorized"
        );
    }
}

std::string CredentialAdministrationService::require_label(const std::string& label) {
    std::string result = trim(label);
    if (result.empty() || result.size() > 80)
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::InvalidRequest, "credential label is invalid");
    return result;
}

std::vector<TargetCredential> CredentialAdministrationService::add_passphrase(
    const std::string& caller,
    const std::string& profile_id,
    int authorization_secret_fd,
    int new_secret_fd,
    const std::string& label
) {
    const ProfileId id(profile_id);
    const std::string checked_label = require_label(label);
    authorize(caller);
    backend_.add_passphrase(id, authorization_secret_fd, new_secret_fd, checked_label);
    return backend_.list_credentials(id);
}

std::vector<TargetCredential> CredentialAdministrationService::add_key(
    const std::string& caller,
    const std::string& profile_id,
    int authorization_secret_fd,
    int key_fd,
    const std::string& label,
    bool automatic
) {
    const ProfileId id(profile_id);
    const std::string checked_label = require_label(label);
    authorize(caller);
    backend_.add_key(id, authorization_secret_fd, key_fd, checked_label, automatic);
    return backend_.list_credentials(id);
}

std::vector<TargetCredential> CredentialAdministrationService::generate_key(
    const std::string& caller,
    const std::string& profile_id,
    int authorization_secret_fd,
    const std::string& label,
    bool automatic
) {
    const ProfileId id(profile_id);
    const std::string checked_label = require_label(label);
    authorize(caller);
    backend_.generate_key(id, authorization_secret_fd, checked_label, automatic);
    return backend_.list_credentials(id);
}

std::vector<TargetCredential> CredentialAdministrationService::remove_credential(
    const std::string& caller,
    const std::string& profile_id,
    const std::string& credential_id,
    int authorization_secret_fd
) {
    const ProfileId id(profile_id);
    if (credential_id.empty())
        throw dbus::ManagerOperationError(dbus::ManagerErrorCode::InvalidRequest, "credential id is required");
    authorize(caller);
    backend_.remove_credential(id, credential_id, authorization_secret_fd);
    return backend_.list_credentials(id);
}

} // namespace btrfsbackup::daemon::control
