// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <iosfwd>

namespace btrfsbackup::command {

void profile_list(
    const std::filesystem::path& profile_config_dir,
    const std::filesystem::path& profile_root_dir,
    std::ostream& output
);

} // namespace btrfsbackup::command
