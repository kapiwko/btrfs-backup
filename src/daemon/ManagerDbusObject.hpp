// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <systemd/sd-bus.h>

#include <cstdint>
#include <exception>
#include <functional>
#include <string>

#include <daemon/ManagerAuditLog.hpp>
#include <daemon/ManagerErrorMapper.hpp>
#include <daemon/ManagerJsonCodec.hpp>
#include <daemon/ManagerService.hpp>
#include <daemon/control/OperationalControlService.hpp>

namespace btrfsbackup::daemon {

class ManagerDbusObject final {
  public:
    ManagerDbusObject(
        ManagerService& service,
        control::OperationalControlService& operational,
        IManagerAuditLog& audit_log
    );

    [[nodiscard]] static const sd_bus_vtable* vtable() noexcept;

    int handle_get_capabilities(sd_bus_message* message, sd_bus_error* error) noexcept;
    int handle_list_profiles(sd_bus_message* message, sd_bus_error* error) noexcept;
    int handle_get_status(sd_bus_message* message, sd_bus_error* error) noexcept;
    int handle_get_history_sanitized(sd_bus_message* message, sd_bus_error* error) noexcept;
    int handle_get_device_state(sd_bus_message* message, sd_bus_error* error) noexcept;
    int handle_start_backup(sd_bus_message* message, sd_bus_error* error) noexcept;
    int handle_cancel_backup(sd_bus_message* message, sd_bus_error* error) noexcept;
    int handle_validate_target(sd_bus_message* message, sd_bus_error* error) noexcept;
    int handle_eject_target(sd_bus_message* message, sd_bus_error* error) noexcept;

  private:
    using JsonOperation = std::function<std::string()>;

    int set_callback_error(sd_bus_error* error, const std::exception* exception);
    static int reply_json(sd_bus_message* message, const std::string& payload);
    int reply_operational_json(
        sd_bus_message* message,
        sd_bus_error* error,
        const std::string& action,
        const std::string& profile_id,
        const JsonOperation& operation
    );
    static std::uint32_t caller_uid(sd_bus_message* message);
    static std::string caller_bus_name(sd_bus_message* message);
    static std::string audit_profile_id(const std::string& profile_id);
    void write_audit_record(
        std::uint32_t uid,
        const std::string& action,
        const std::string& profile_id,
        const std::string& result,
        const std::string& error_code
    );

    ManagerService& service_;
    control::OperationalControlService& operational_;
    IManagerAuditLog& audit_log_;
    ManagerJsonCodec codec_;
    ManagerErrorMapper error_mapper_;
};

} // namespace btrfsbackup::daemon
