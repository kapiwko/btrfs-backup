// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace btrfsbackup::cli::upgrade {

int upgrade(
    const std::filesystem::path& profile_config_dir,
    const std::vector<std::string>& args,
    std::ostream& output,
    std::ostream& error
);

} // namespace btrfsbackup::cli::upgrade
