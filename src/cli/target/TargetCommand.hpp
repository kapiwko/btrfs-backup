// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

#include <cli/target/TargetService.hpp>

namespace btrfsbackup::cli::target {

using TargetExecutionServices = TargetServiceDependencies;

int target(
    const std::filesystem::path& profile_config_dir,
    const std::vector<std::string>& args,
    std::ostream& output
);

int target(
    const std::filesystem::path& profile_config_dir,
    const std::vector<std::string>& args,
    std::ostream& output,
    TargetExecutionServices* services
);

} // namespace btrfsbackup::cli::target
