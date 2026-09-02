// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace btrfsbackup::cli::repository {

int repository(const std::filesystem::path& config_root, const std::vector<std::string>& args, std::ostream& output);

} // namespace btrfsbackup::cli::repository
