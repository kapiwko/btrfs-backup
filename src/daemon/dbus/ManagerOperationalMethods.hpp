// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <systemd/sd-bus.h>

#include <daemon/dbus/ManagerMethodSupport.hpp>

namespace btrfsbackup::daemon::control {
class OperationalControlService;
}

namespace btrfsbackup::daemon::dbus {

class ManagerOperationalMethods final {
  public:
    ManagerOperationalMethods(control::OperationalControlService& operational, ManagerMethodSupport& support);

    int start_backup(sd_bus_message* message, sd_bus_error* error) noexcept;
    int cancel_backup(sd_bus_message* message, sd_bus_error* error) noexcept;
    int validate_target(sd_bus_message* message, sd_bus_error* error) noexcept;
    int eject_target(sd_bus_message* message, sd_bus_error* error) noexcept;

  private:
    control::OperationalControlService& operational_;
    ManagerMethodSupport& support_;
};

} // namespace btrfsbackup::daemon::dbus
