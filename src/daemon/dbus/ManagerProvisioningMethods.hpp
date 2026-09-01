// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <systemd/sd-bus.h>

#include <daemon/dbus/ManagerMethodSupport.hpp>

namespace btrfsbackup::daemon::control {
class DeviceProvisioningService;
}

namespace btrfsbackup::daemon::dbus {

class ManagerProvisioningMethods final {
  public:
    ManagerProvisioningMethods(
        control::DeviceProvisioningService& device_provisioning,
        ManagerMethodSupport& support
    );

    int list_provisioning_devices(sd_bus_message* message, sd_bus_error* error) noexcept;
    int inspect_storage_topology(sd_bus_message* message, sd_bus_error* error) noexcept;
    int build_device_preparation_plan(sd_bus_message* message, sd_bus_error* error) noexcept;
    int list_source_candidates(sd_bus_message* message, sd_bus_error* error) noexcept;
    int start_device_preparation(sd_bus_message* message, sd_bus_error* error) noexcept;
    int get_device_preparation(sd_bus_message* message, sd_bus_error* error) noexcept;
    int cancel_device_preparation(sd_bus_message* message, sd_bus_error* error) noexcept;

  private:
    control::DeviceProvisioningService& device_provisioning_;
    ManagerMethodSupport& support_;
};

} // namespace btrfsbackup::daemon::dbus
