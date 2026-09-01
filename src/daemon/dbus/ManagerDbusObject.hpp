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
#include <daemon/dbus/ManagerErrorMapper.hpp>
#include <daemon/dbus/ManagerJsonCodec.hpp>
#include <daemon/ManagerService.hpp>
#include <daemon/control/OperationalControlService.hpp>
#include <daemon/control/BrowseSessionService.hpp>
#include <daemon/control/ProfileAdministrationService.hpp>
#include <daemon/control/CredentialAdministrationService.hpp>
#include <daemon/control/DeviceProvisioningService.hpp>

namespace btrfsbackup::daemon::dbus {

class ManagerDbusObject final {
  public:
    ManagerDbusObject(
        ManagerService& service,
        control::OperationalControlService& operational,
        control::BrowseSessionService& browse_sessions,
        control::ProfileAdministrationService& profile_administration,
        control::CredentialAdministrationService& credential_administration,
        control::DeviceProvisioningService& device_provisioning,
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
    int handle_get_profile_details(sd_bus_message* message, sd_bus_error* error) noexcept;
    int handle_update_profile_settings(sd_bus_message* message, sd_bus_error* error) noexcept;
    int handle_add_profile_source(sd_bus_message* message, sd_bus_error* error) noexcept;
    int handle_update_profile_source(sd_bus_message* message, sd_bus_error* error) noexcept;
    int handle_remove_profile_source(sd_bus_message* message, sd_bus_error* error) noexcept;
    int handle_delete_profile(sd_bus_message* message, sd_bus_error* error) noexcept;
    int handle_set_profile_enabled(sd_bus_message* message, sd_bus_error* error) noexcept;
    int handle_open_browse_session(sd_bus_message* message, sd_bus_error* error) noexcept;
    int handle_renew_browse_session(sd_bus_message* message, sd_bus_error* error) noexcept;
    int handle_set_browse_session_active(sd_bus_message* message, sd_bus_error* error) noexcept;
    int handle_close_browse_session(sd_bus_message* message, sd_bus_error* error) noexcept;
    int handle_resolve_backup_coverage(sd_bus_message* message, sd_bus_error* error) noexcept;
    int handle_list_target_credentials(sd_bus_message* message, sd_bus_error* error) noexcept;
    int handle_add_target_passphrase(sd_bus_message* message, sd_bus_error* error) noexcept;
    int handle_add_target_key(sd_bus_message* message, sd_bus_error* error) noexcept;
    int handle_generate_target_key(sd_bus_message* message, sd_bus_error* error) noexcept;
    int handle_remove_target_credential(sd_bus_message* message, sd_bus_error* error) noexcept;
    int handle_list_provisioning_devices(sd_bus_message* message, sd_bus_error* error) noexcept;
    int handle_list_source_candidates(sd_bus_message* message, sd_bus_error* error) noexcept;
    int handle_start_device_preparation(sd_bus_message* message, sd_bus_error* error) noexcept;
    int handle_get_device_preparation(sd_bus_message* message, sd_bus_error* error) noexcept;
    int handle_cancel_device_preparation(sd_bus_message* message, sd_bus_error* error) noexcept;

  private:
    using JsonOperation = std::function<std::string()>;

    int set_callback_error(sd_bus_error* error, const std::exception* exception);
    static int reply_json(sd_bus_message* message, const std::string& payload);
    static void emit_device_state_changed(sd_bus_message* message, const std::string& profile_id) noexcept;
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
    control::BrowseSessionService& browse_sessions_;
    control::ProfileAdministrationService& profile_administration_;
    control::CredentialAdministrationService& credential_administration_;
    control::DeviceProvisioningService& device_provisioning_;
    IManagerAuditLog& audit_log_;
    ManagerJsonCodec codec_;
    ManagerErrorMapper error_mapper_;
};

} // namespace btrfsbackup::daemon::dbus
