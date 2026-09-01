// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <systemd/sd-bus.h>

#include <daemon/dbus/ManagerMethodSupport.hpp>

namespace btrfsbackup::daemon::control {
class ProfileAdministrationService;
}

namespace btrfsbackup::daemon::dbus {

class ManagerProfileMethods final {
  public:
    ManagerProfileMethods(
        control::ProfileAdministrationService& profile_administration,
        ManagerMethodSupport& support
    );

    int get_profile_details(sd_bus_message* message, sd_bus_error* error) noexcept;
    int update_profile_settings(sd_bus_message* message, sd_bus_error* error) noexcept;
    int add_profile_source(sd_bus_message* message, sd_bus_error* error) noexcept;
    int update_profile_source(sd_bus_message* message, sd_bus_error* error) noexcept;
    int remove_profile_source(sd_bus_message* message, sd_bus_error* error) noexcept;
    int delete_profile(sd_bus_message* message, sd_bus_error* error) noexcept;
    int set_profile_enabled(sd_bus_message* message, sd_bus_error* error) noexcept;

  private:
    control::ProfileAdministrationService& profile_administration_;
    ManagerMethodSupport& support_;
};

} // namespace btrfsbackup::daemon::dbus
