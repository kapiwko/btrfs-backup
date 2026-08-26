// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

#include <backup/ports/process.hpp>

namespace btrfsbackup {

class ICommandRunner {
  public:
    virtual ~ICommandRunner() = default;
    [[nodiscard]] virtual CommandResult run(const std::vector<std::string>& argv) = 0;
    [[nodiscard]] virtual CommandResult run_controlled(
        const std::vector<std::string>& argv,
        const ControlledCommandOptions& options
    ) = 0;
};

[[nodiscard]] std::string capture_command(ICommandRunner& runner, const std::vector<std::string>& argv);

} // namespace btrfsbackup
