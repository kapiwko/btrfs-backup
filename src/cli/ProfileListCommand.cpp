// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cli/ProfileListCommand.hpp>

#include <filesystem>
#include <ostream>
#include <string>

#include <platform/linux/config/ProfileService.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::cli {

void profile_list(const fs::path& profile_root_dir, std::ostream& output) {
    for (const std::string& profile : btrfsbackup::platform::linux::list_profiles(profile_root_dir)) {
        output << profile << '\n';
    }
}

} // namespace btrfsbackup::cli
