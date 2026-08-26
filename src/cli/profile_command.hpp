// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include <config/ports/configuration_activator.hpp>

namespace btrfsbackup::command {

int profile(
    const std::vector<std::string>& args,
    const std::filesystem::path& profile_config_dir,
    IConfigurationActivator& system_activator
);

} // namespace btrfsbackup::command
