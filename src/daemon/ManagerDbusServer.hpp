// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include <daemon/ManagerAuditLog.hpp>
#include <daemon/ManagerPaths.hpp>
#include <daemon/ManagerService.hpp>
#include <daemon/OperationalControlService.hpp>

namespace btrfsbackup::daemon {

inline constexpr const char* manager_bus_name = "io.github.btrfsbackup.Manager1";
inline constexpr const char* manager_object_path = "/io/github/btrfsbackup/Manager1";
inline constexpr const char* manager_interface = "io.github.btrfsbackup.Manager1";

int run_dbus_server(
    ManagerService& service,
    IOperationalControlBackend& operational_backend,
    IManagerAuditLog& audit_log,
    const ManagerPaths& paths,
    const std::string& bus_address = {}
);

} // namespace btrfsbackup::daemon
