// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

#include <backup/ports/process.hpp>

namespace btrfsbackup {

CommandResult run_command(const std::vector<std::string>& argv);
CommandResult run_controlled_command(
    const std::vector<std::string>& argv,
    const ControlledCommandOptions& options
);
std::string run_capture(const std::vector<std::string>& argv);

} // namespace btrfsbackup
