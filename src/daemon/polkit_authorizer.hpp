// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include <daemon/operational_control_service.hpp>

struct sd_bus;

namespace btrfsbackup::daemon {

class PolkitAuthorizer final : public IManagerAuthorizer {
  public:
    explicit PolkitAuthorizer(sd_bus* bus);

    [[nodiscard]] bool authorize(
        const std::string& caller_bus_name,
        ManagerAuthorizationAction action
    ) override;

  private:
    sd_bus* bus_;
};

} // namespace btrfsbackup::daemon
