// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace btrfsbackup::config {

inline constexpr std::string_view current_configuration_fingerprint_version = "2.0.0";

std::string compute_config_fingerprint_from_bytes(
    std::string_view version,
    const std::filesystem::path& config_file,
    std::string_view contents
);

std::string compute_config_fingerprint(
    const std::string& version,
    const std::filesystem::path& config_file,
    const std::vector<std::filesystem::path>& source_files
);

} // namespace btrfsbackup::config
