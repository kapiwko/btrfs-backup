// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <systemd/sd-bus.h>

#include <daemon/ManagerAuditLog.hpp>
#include <daemon/ManagerErrorMapper.hpp>
#include <daemon/ManagerJsonCodec.hpp>
#include <daemon/ManagerService.hpp>
#include <daemon/OperationalControlService.hpp>

namespace btrfsbackup::daemon {

class ManagerDbusObject final {
  public:
    ManagerService& service;
    OperationalControlService& operational;
    IManagerAuditLog& audit_log;
    ManagerJsonCodec codec;
    ManagerErrorMapper error_mapper;

    [[nodiscard]] const sd_bus_vtable* vtable() const;
};

} // namespace btrfsbackup::daemon
