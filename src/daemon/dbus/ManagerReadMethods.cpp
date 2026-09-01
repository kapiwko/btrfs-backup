// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/dbus/ManagerReadMethods.hpp>

#include <daemon/ManagerService.hpp>
#include <daemon/control/ProfileAdministrationService.hpp>
#include <daemon/dbus/DbusCallbackBoundary.hpp>

namespace btrfsbackup::daemon::dbus {

ManagerReadMethods::ManagerReadMethods(
    ManagerService& service,
    control::ProfileAdministrationService& profile_administration,
    ManagerMethodSupport& support
)
    : service_(service),
      profile_administration_(profile_administration),
      support_(support) {
}

int ManagerReadMethods::get_capabilities(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            return ManagerMethodSupport::reply_json(
                message,
                support_.codec().encode(service_.get_capabilities())
            );
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

int ManagerReadMethods::list_profiles(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            auto profiles = service_.list_profiles();
            for (auto& profile : profiles) {
                const auto health = profile_administration_.configuration_health(profile.profile_id);
                profile.configuration_valid = health.valid;
                profile.configuration_error_code = health.error_code;
            }
            return ManagerMethodSupport::reply_json(message, support_.codec().encode(profiles));
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

int ManagerReadMethods::get_status(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const int read_result = sd_bus_message_read(message, "s", &profile_id);
            if (read_result < 0)
                return read_result;
            return ManagerMethodSupport::reply_json(
                message,
                support_.codec().encode(service_.get_status(profile_id == nullptr ? "" : profile_id))
            );
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

int ManagerReadMethods::get_history_sanitized(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            std::uint32_t offset = 0;
            std::uint32_t limit = 0;
            const int read_result = sd_bus_message_read(message, "suu", &profile_id, &offset, &limit);
            if (read_result < 0)
                return read_result;
            return ManagerMethodSupport::reply_json(
                message,
                support_.codec().encode(service_.get_history_sanitized(
                    profile_id == nullptr ? "" : profile_id,
                    offset,
                    limit
                ))
            );
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

int ManagerReadMethods::get_device_state(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const int read_result = sd_bus_message_read(message, "s", &profile_id);
            if (read_result < 0)
                return read_result;
            return ManagerMethodSupport::reply_json(
                message,
                support_.codec().encode(service_.get_device_state(profile_id == nullptr ? "" : profile_id))
            );
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

} // namespace btrfsbackup::daemon::dbus
