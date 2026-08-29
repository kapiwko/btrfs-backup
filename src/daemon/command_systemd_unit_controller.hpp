// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/ports/command_runner.hpp>
#include <daemon/systemd_unit_controller.hpp>

namespace btrfsbackup::daemon {

class CommandSystemdUnitController final : public ISystemdUnitController {
  public:
    explicit CommandSystemdUnitController(btrfsbackup::backup::ICommandRunner& commands);

    [[nodiscard]] StartJobResult start_unit(const StartUnitRequest& request) override;
    [[nodiscard]] StopJobResult stop_unit(const StopUnitRequest& request) override;
    [[nodiscard]] TransientJobResult start_transient_unit(
        const TransientUnitRequest& request
    ) override;

  private:
    btrfsbackup::backup::ICommandRunner& commands_;
};

} // namespace btrfsbackup::daemon
