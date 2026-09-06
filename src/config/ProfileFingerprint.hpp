// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string_view>

namespace btrfsbackup::config {

inline constexpr std::string_view current_configuration_fingerprint_version = "1.0.0";

std::string compute_config_fingerprint_from_bytes(
    std::string_view version,
    const std::filesystem::path& config_file,
    std::string_view contents
);

} // namespace btrfsbackup::config
