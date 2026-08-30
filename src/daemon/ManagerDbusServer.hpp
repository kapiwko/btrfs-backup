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

namespace btrfsbackup::daemon {

int run_dbus_server(
    ManagerService& service,
    control::IOperationalControlBackend& operational_backend,
    IManagerAuditLog& audit_log,
    const ManagerPaths& paths,
    const std::string& bus_address = {}
);

} // namespace btrfsbackup::daemon
