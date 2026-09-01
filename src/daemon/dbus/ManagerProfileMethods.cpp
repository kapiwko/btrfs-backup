// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/dbus/ManagerProfileMethods.hpp>

#include <daemon/control/ProfileAdministrationService.hpp>
#include <daemon/dbus/DbusCallbackBoundary.hpp>

#include <config/json/JsonIo.hpp>
#include <core/ManagerProtocol.hpp>

namespace btrfsbackup::daemon::dbus {

ManagerProfileMethods::ManagerProfileMethods(
    control::ProfileAdministrationService& profile_administration,
    ManagerMethodSupport& support
)
    : profile_administration_(profile_administration),
      support_(support) {
}

int ManagerProfileMethods::get_profile_details(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const int read_result = sd_bus_message_read(message, "s", &profile_id);
            if (read_result < 0)
                return read_result;
            const std::string profile = profile_id == nullptr ? "" : profile_id;
            return ManagerMethodSupport::reply_json(
                message,
                support_.codec().encode(profile_administration_.get_profile_details(profile))
            );
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

int ManagerProfileMethods::update_profile_settings(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const char* generation = nullptr;
            const char* fingerprint = nullptr;
            const char* request = nullptr;
            const int read_result = sd_bus_message_read(message, "ssss", &profile_id, &generation, &fingerprint, &request);
            if (read_result < 0)
                return read_result;
            const std::string profile = profile_id == nullptr ? "" : profile_id;
            return support_.reply_operational_json(message, error, "update-profile-settings", profile, [&] {
                return support_.codec().encode(profile_administration_.update_profile_settings(
                    ManagerMethodSupport::caller_bus_name(message),
                    profile,
                    generation == nullptr ? "" : generation,
                    fingerprint == nullptr ? "" : fingerprint,
                    request == nullptr ? "" : request
                ));
            });
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

int ManagerProfileMethods::add_profile_source(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const char* generation = nullptr;
            const char* fingerprint = nullptr;
            const char* request = nullptr;
            const int read_result = sd_bus_message_read(message, "ssss", &profile_id, &generation, &fingerprint, &request);
            if (read_result < 0)
                return read_result;
            const std::string profile = profile_id == nullptr ? "" : profile_id;
            return support_.reply_operational_json(message, error, "add-profile-source", profile, [&] {
                return support_.codec().encode(profile_administration_.add_profile_source(
                    ManagerMethodSupport::caller_bus_name(message),
                    profile,
                    generation == nullptr ? "" : generation,
                    fingerprint == nullptr ? "" : fingerprint,
                    request == nullptr ? "" : request
                ));
            });
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

int ManagerProfileMethods::update_profile_source(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const char* source_id = nullptr;
            const char* generation = nullptr;
            const char* fingerprint = nullptr;
            const char* request = nullptr;
            const int read_result = sd_bus_message_read(
                message,
                "sssss",
                &profile_id,
                &source_id,
                &generation,
                &fingerprint,
                &request
            );
            if (read_result < 0)
                return read_result;
            const std::string profile = profile_id == nullptr ? "" : profile_id;
            return support_.reply_operational_json(message, error, "update-profile-source", profile, [&] {
                return support_.codec().encode(profile_administration_.update_profile_source(
                    ManagerMethodSupport::caller_bus_name(message),
                    profile,
                    source_id == nullptr ? "" : source_id,
                    generation == nullptr ? "" : generation,
                    fingerprint == nullptr ? "" : fingerprint,
                    request == nullptr ? "" : request
                ));
            });
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

int ManagerProfileMethods::remove_profile_source(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const char* source_id = nullptr;
            const char* generation = nullptr;
            const char* fingerprint = nullptr;
            const int read_result = sd_bus_message_read(message, "ssss", &profile_id, &source_id, &generation, &fingerprint);
            if (read_result < 0)
                return read_result;
            const std::string profile = profile_id == nullptr ? "" : profile_id;
            return support_.reply_operational_json(message, error, "remove-profile-source", profile, [&] {
                return support_.codec().encode(profile_administration_.remove_profile_source(
                    ManagerMethodSupport::caller_bus_name(message),
                    profile,
                    source_id == nullptr ? "" : source_id,
                    generation == nullptr ? "" : generation,
                    fingerprint == nullptr ? "" : fingerprint
                ));
            });
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

int ManagerProfileMethods::delete_profile(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const char* generation = nullptr;
            const char* fingerprint = nullptr;
            const int read_result = sd_bus_message_read(message, "sss", &profile_id, &generation, &fingerprint);
            if (read_result < 0)
                return read_result;
            const std::string profile = profile_id == nullptr ? "" : profile_id;
            return support_.reply_operational_json(message, error, "delete-profile", profile, [&] {
                profile_administration_.delete_profile(
                    ManagerMethodSupport::caller_bus_name(message),
                    profile,
                    generation == nullptr ? "" : generation,
                    fingerprint == nullptr ? "" : fingerprint
                );
                return config::json::dump_json({
                    {"schemaVersion", manager_protocol::operation_result_schema_version},
                    {"operation", "delete-profile"},
                    {"profileId", profile},
                    {"accepted", true},
                });
            });
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

int ManagerProfileMethods::set_profile_enabled(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            int enabled = 0;
            const int read_result = sd_bus_message_read(message, "sb", &profile_id, &enabled);
            if (read_result < 0)
                return read_result;
            const std::string profile = profile_id == nullptr ? "" : profile_id;
            return support_.reply_operational_json(
                message,
                error,
                manager_protocol::feature::profile_activation,
                profile,
                [&] {
                    profile_administration_.set_profile_enabled(
                        ManagerMethodSupport::caller_bus_name(message),
                        profile,
                        enabled != 0
                    );
                    return config::json::dump_json({
                        {"schemaVersion", manager_protocol::operation_result_schema_version},
                        {"operation", manager_protocol::feature::profile_activation},
                        {"profileId", profile},
                        {"enabled", enabled != 0},
                        {"accepted", true},
                    });
                }
            );
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

} // namespace btrfsbackup::daemon::dbus
