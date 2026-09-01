// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <systemd/sd-bus.h>

#include <daemon/dbus/ManagerMethodSupport.hpp>

namespace btrfsbackup::daemon::control {
class CredentialAdministrationService;
}

namespace btrfsbackup::daemon::dbus {

class ManagerCredentialMethods final {
  public:
    ManagerCredentialMethods(
        control::CredentialAdministrationService& credential_administration,
        ManagerMethodSupport& support
    );

    int list_target_credentials(sd_bus_message* message, sd_bus_error* error) noexcept;
    int add_target_passphrase(sd_bus_message* message, sd_bus_error* error) noexcept;
    int add_target_key(sd_bus_message* message, sd_bus_error* error) noexcept;
    int generate_target_key(sd_bus_message* message, sd_bus_error* error) noexcept;
    int remove_target_credential(sd_bus_message* message, sd_bus_error* error) noexcept;

  private:
    control::CredentialAdministrationService& credential_administration_;
    ManagerMethodSupport& support_;
};

} // namespace btrfsbackup::daemon::dbus
