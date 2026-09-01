// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <systemd/sd-bus.h>

#include <daemon/dbus/ManagerMethodSupport.hpp>

namespace btrfsbackup::daemon {
class ManagerService;
namespace control {
class ProfileAdministrationService;
}
} // namespace btrfsbackup::daemon

namespace btrfsbackup::daemon::dbus {

class ManagerReadMethods final {
  public:
    ManagerReadMethods(
        ManagerService& service,
        control::ProfileAdministrationService& profile_administration,
        ManagerMethodSupport& support
    );

    int get_capabilities(sd_bus_message* message, sd_bus_error* error) noexcept;
    int list_profiles(sd_bus_message* message, sd_bus_error* error) noexcept;
    int get_status(sd_bus_message* message, sd_bus_error* error) noexcept;
    int get_history_sanitized(sd_bus_message* message, sd_bus_error* error) noexcept;
    int get_device_state(sd_bus_message* message, sd_bus_error* error) noexcept;

  private:
    ManagerService& service_;
    control::ProfileAdministrationService& profile_administration_;
    ManagerMethodSupport& support_;
};

} // namespace btrfsbackup::daemon::dbus
