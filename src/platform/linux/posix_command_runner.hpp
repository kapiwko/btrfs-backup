// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/ports/command_runner.hpp>
#include <platform/linux/process.hpp>

namespace btrfsbackup::platform::linux {

class PosixCommandRunner final : public btrfsbackup::backup::ICommandRunner {
  public:
    [[nodiscard]] btrfsbackup::backup::CommandResult run(const std::vector<std::string>& argv) override;
    [[nodiscard]] btrfsbackup::backup::CommandResult run_controlled(
        const std::vector<std::string>& argv,
        const btrfsbackup::backup::ControlledCommandOptions& options
    ) override;
};

} // namespace btrfsbackup::platform::linux
