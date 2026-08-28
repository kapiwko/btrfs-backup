// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace btrfsbackup::config {

std::string compute_config_fingerprint(
    const std::string& version,
    const std::filesystem::path& config_file,
    const std::vector<std::filesystem::path>& source_files
);

} // namespace btrfsbackup::config
