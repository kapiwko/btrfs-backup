// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include <daemon/control/OperationalControlService.hpp>

struct sd_bus;

namespace btrfsbackup::daemon::dbus {

class PolkitAuthorizer final : public control::IManagerAuthorizer {
  public:
    explicit PolkitAuthorizer(sd_bus* bus);

    [[nodiscard]] bool authorize(
        const std::string& caller_bus_name,
        control::ManagerAuthorizationAction action
    ) override;
    [[nodiscard]] bool caller_is_active(const std::string& caller_bus_name) override;

  private:
    sd_bus* bus_;
};

} // namespace btrfsbackup::daemon::dbus
