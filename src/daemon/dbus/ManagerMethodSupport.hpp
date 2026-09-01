// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <systemd/sd-bus.h>

#include <cstdint>
#include <exception>
#include <functional>
#include <string>

#include <daemon/ManagerAuditLog.hpp>
#include <daemon/dbus/ManagerErrorMapper.hpp>
#include <daemon/dbus/ManagerJsonCodec.hpp>

namespace btrfsbackup::daemon::dbus {

class ManagerMethodSupport final {
  public:
    using JsonOperation = std::function<std::string()>;

    explicit ManagerMethodSupport(IManagerAuditLog& audit_log);

    [[nodiscard]] ManagerJsonCodec& codec() noexcept;
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
    static void emit_device_state_changed(sd_bus_message* message, const std::string& profile_id) noexcept;

  private:
    static std::string audit_profile_id(const std::string& profile_id);
    void write_audit_record(
        std::uint32_t uid,
        const std::string& action,
        const std::string& profile_id,
        const std::string& result,
        const std::string& error_code
    );

    IManagerAuditLog& audit_log_;
    ManagerJsonCodec codec_;
    ManagerErrorMapper error_mapper_;
};

} // namespace btrfsbackup::daemon::dbus
