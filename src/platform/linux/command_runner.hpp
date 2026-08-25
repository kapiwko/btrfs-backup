// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/ports/command_runner.hpp>
#include <platform/linux/process.hpp>

namespace btrfsbackup {

class PosixCommandRunner final : public ICommandRunner {
public:
    CommandResult run(const std::vector<std::string>& argv) override;
    CommandResult run_controlled(
        const std::vector<std::string>& argv,
        const ControlledCommandOptions& options
    ) override;
};

} // namespace btrfsbackup
