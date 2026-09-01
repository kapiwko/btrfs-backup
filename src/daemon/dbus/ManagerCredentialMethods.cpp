// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/dbus/ManagerCredentialMethods.hpp>

#include <daemon/control/CredentialAdministrationService.hpp>
#include <daemon/dbus/DbusCallbackBoundary.hpp>

namespace btrfsbackup::daemon::dbus {

ManagerCredentialMethods::ManagerCredentialMethods(
    control::CredentialAdministrationService& credential_administration,
    ManagerMethodSupport& support
)
    : credential_administration_(credential_administration),
      support_(support) {
}

int ManagerCredentialMethods::list_target_credentials(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const int read_result = sd_bus_message_read(message, "s", &profile_id);
            if (read_result < 0)
                return read_result;
            const std::string profile = profile_id == nullptr ? "" : profile_id;
            return support_.reply_operational_json(message, error, "list-target-credentials", profile, [&] {
                return support_.codec().encode(credential_administration_.list_credentials(
                    ManagerMethodSupport::caller_bus_name(message),
                    profile
                ));
            });
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

int ManagerCredentialMethods::add_target_passphrase(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const char* label = nullptr;
            int authorization_fd = -1;
            int new_secret_fd = -1;
            const int read_result = sd_bus_message_read(
                message,
                "shhs",
                &profile_id,
                &authorization_fd,
                &new_secret_fd,
                &label
            );
            if (read_result < 0)
                return read_result;
            const std::string profile = profile_id == nullptr ? "" : profile_id;
            return support_.reply_operational_json(message, error, "add-target-passphrase", profile, [&] {
                return support_.codec().encode(credential_administration_.add_passphrase(
                    ManagerMethodSupport::caller_bus_name(message),
                    profile,
                    authorization_fd,
                    new_secret_fd,
                    label == nullptr ? "" : label
                ));
            });
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

int ManagerCredentialMethods::add_target_key(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const char* label = nullptr;
            int authorization_fd = -1;
            int key_fd = -1;
            int automatic = 0;
            const int read_result = sd_bus_message_read(
                message,
                "shhsb",
                &profile_id,
                &authorization_fd,
                &key_fd,
                &label,
                &automatic
            );
            if (read_result < 0)
                return read_result;
            const std::string profile = profile_id == nullptr ? "" : profile_id;
            return support_.reply_operational_json(message, error, "add-target-key", profile, [&] {
                return support_.codec().encode(credential_administration_.add_key(
                    ManagerMethodSupport::caller_bus_name(message),
                    profile,
                    authorization_fd,
                    key_fd,
                    label == nullptr ? "" : label,
                    automatic != 0
                ));
            });
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

int ManagerCredentialMethods::generate_target_key(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const char* label = nullptr;
            int authorization_fd = -1;
            int automatic = 0;
            const int read_result = sd_bus_message_read(
                message,
                "shsb",
                &profile_id,
                &authorization_fd,
                &label,
                &automatic
            );
            if (read_result < 0)
                return read_result;
            const std::string profile = profile_id == nullptr ? "" : profile_id;
            return support_.reply_operational_json(message, error, "generate-target-key", profile, [&] {
                return support_.codec().encode(credential_administration_.generate_key(
                    ManagerMethodSupport::caller_bus_name(message),
                    profile,
                    authorization_fd,
                    label == nullptr ? "" : label,
                    automatic != 0
                ));
            });
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

int ManagerCredentialMethods::remove_target_credential(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const char* credential_id = nullptr;
            int authorization_fd = -1;
            const int read_result = sd_bus_message_read(
                message,
                "ssh",
                &profile_id,
                &credential_id,
                &authorization_fd
            );
            if (read_result < 0)
                return read_result;
            const std::string profile = profile_id == nullptr ? "" : profile_id;
            return support_.reply_operational_json(message, error, "remove-target-credential", profile, [&] {
                return support_.codec().encode(credential_administration_.remove_credential(
                    ManagerMethodSupport::caller_bus_name(message),
                    profile,
                    credential_id == nullptr ? "" : credential_id,
                    authorization_fd
                ));
            });
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

} // namespace btrfsbackup::daemon::dbus
