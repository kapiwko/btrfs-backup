// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

#include <backup/ports/process.hpp>

namespace btrfsbackup {

[[nodiscard]] CommandResult run_command(const std::vector<std::string>& argv);
[[nodiscard]] CommandResult run_controlled_command(
    const std::vector<std::string>& argv,
    const ControlledCommandOptions& options
);
[[nodiscard]] std::string run_capture(const std::vector<std::string>& argv);

} // namespace btrfsbackup
