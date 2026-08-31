// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cli/profile/ProfileListCommand.hpp>

#include <filesystem>
#include <ostream>
#include <string>
#include <vector>

#include <core/Errors.hpp>
#include <platform/linux/config/ProfileService.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::cli::profile {

void profile_list(const fs::path& profile_root_dir, std::ostream& output) {
    const std::vector<std::string> profiles =
        btrfsbackup::platform::linux::config::list_profiles(profile_root_dir);
    if (profiles.empty())
        throw ValidationError("no profiles found");
    for (const std::string& profile : profiles) {
        output << profile << '\n';
    }
}

} // namespace btrfsbackup::cli::profile
