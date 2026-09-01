// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/dbus/ManagerOperationalMethods.hpp>

#include <daemon/control/OperationalControlService.hpp>
#include <daemon/dbus/DbusCallbackBoundary.hpp>

#include <core/ManagerProtocol.hpp>

namespace btrfsbackup::daemon::dbus {

ManagerOperationalMethods::ManagerOperationalMethods(
    control::OperationalControlService& operational,
    ManagerMethodSupport& support
)
    : operational_(operational),
      support_(support) {
}

int ManagerOperationalMethods::start_backup(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const int read_result = sd_bus_message_read(message, "s", &profile_id);
            if (read_result < 0)
                return read_result;
            const std::string profile = profile_id == nullptr ? "" : profile_id;
            return support_.reply_operational_json(
                message,
                error,
                manager_protocol::feature::start_backup,
                profile,
                [&] {
                    return support_.codec().encode(operational_.start_backup(
                        ManagerMethodSupport::caller_bus_name(message),
                        profile
                    ));
                }
            );
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

int ManagerOperationalMethods::cancel_backup(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const char* run_id = nullptr;
            const int read_result = sd_bus_message_read(message, "ss", &profile_id, &run_id);
            if (read_result < 0)
                return read_result;
            const std::string profile = profile_id == nullptr ? "" : profile_id;
            return support_.reply_operational_json(
                message,
                error,
                manager_protocol::feature::cancel_backup,
                profile,
                [&] {
                    return support_.codec().encode(operational_.cancel_backup(
                        ManagerMethodSupport::caller_bus_name(message),
                        profile,
                        run_id == nullptr ? "" : run_id
                    ));
                }
            );
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

int ManagerOperationalMethods::validate_target(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const int read_result = sd_bus_message_read(message, "s", &profile_id);
            if (read_result < 0)
                return read_result;
            const std::string profile = profile_id == nullptr ? "" : profile_id;
            return support_.reply_operational_json(
                message,
                error,
                manager_protocol::feature::validate_target,
                profile,
                [&] {
                    return support_.codec().encode(operational_.validate_target(
                        ManagerMethodSupport::caller_bus_name(message),
                        profile
                    ));
                }
            );
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

int ManagerOperationalMethods::eject_target(sd_bus_message* message, sd_bus_error* error) noexcept {
    return invoke_dbus_callback(
        [&] {
            const char* profile_id = nullptr;
            const int read_result = sd_bus_message_read(message, "s", &profile_id);
            if (read_result < 0)
                return read_result;
            const std::string profile = profile_id == nullptr ? "" : profile_id;
            return support_.reply_operational_json(
                message,
                error,
                manager_protocol::feature::eject_target,
                profile,
                [&] {
                    const auto result = operational_.eject_target(
                        ManagerMethodSupport::caller_bus_name(message),
                        profile
                    );
                    ManagerMethodSupport::emit_device_state_changed(message, profile);
                    return support_.codec().encode(result);
                }
            );
        },
        [&](const std::exception* exception) { return support_.set_callback_error(error, exception); }
    );
}

} // namespace btrfsbackup::daemon::dbus
