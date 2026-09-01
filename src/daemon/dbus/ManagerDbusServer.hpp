// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include <core/ManagerProtocol.hpp>
#include <daemon/ManagerAuditLog.hpp>
#include <daemon/ManagerPaths.hpp>
#include <daemon/ManagerService.hpp>
#include <daemon/control/OperationalControlService.hpp>
#include <daemon/control/ProfileAdministrationService.hpp>
#include <daemon/control/BrowseSessionService.hpp>
#include <daemon/control/CredentialAdministrationService.hpp>
#include <daemon/control/DeviceProvisioningService.hpp>

namespace btrfsbackup::daemon::dbus {

int run_dbus_server(
    ManagerService& service,
    control::IOperationalControlBackend& operational_backend,
    control::IProfileAdministrationBackend& profile_administration_backend,
    control::ICredentialAdministrationBackend& credential_administration_backend,
    control::IDeviceProvisioningBackend& device_provisioning_backend,
    control::IBrowseSessionBackend& browse_session_backend,
    IManagerAuditLog& audit_log,
    const ManagerPaths& paths,
    const std::string& bus_address = {}
);

} // namespace btrfsbackup::daemon::dbus
