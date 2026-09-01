// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <systemd/sd-bus.h>

#include <daemon/dbus/ManagerBrowseMethods.hpp>
#include <daemon/dbus/ManagerCredentialMethods.hpp>
#include <daemon/dbus/ManagerOperationalMethods.hpp>
#include <daemon/dbus/ManagerProfileMethods.hpp>
#include <daemon/dbus/ManagerProvisioningMethods.hpp>
#include <daemon/dbus/ManagerReadMethods.hpp>

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

    [[nodiscard]] ManagerReadMethods& read_methods() noexcept;
    [[nodiscard]] ManagerOperationalMethods& operational_methods() noexcept;
    [[nodiscard]] ManagerProfileMethods& profile_methods() noexcept;
    [[nodiscard]] ManagerBrowseMethods& browse_methods() noexcept;
    [[nodiscard]] ManagerCredentialMethods& credential_methods() noexcept;
    [[nodiscard]] ManagerProvisioningMethods& provisioning_methods() noexcept;

  private:
    ManagerMethodSupport support_;
    ManagerReadMethods read_methods_;
    ManagerOperationalMethods operational_methods_;
    ManagerProfileMethods profile_methods_;
    ManagerBrowseMethods browse_methods_;
    ManagerCredentialMethods credential_methods_;
    ManagerProvisioningMethods provisioning_methods_;
};

} // namespace btrfsbackup::daemon::dbus
