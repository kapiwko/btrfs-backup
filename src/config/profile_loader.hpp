// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>

#include <config/json.hpp>
#include <config/profile.hpp>

namespace btrfsbackup {

std::filesystem::path profile_json_path(const std::filesystem::path& etc_root, const std::string& profile_id);
Json load_profile_json_by_id(const std::filesystem::path& etc_root, const std::string& profile_id);
Profile load_profile_by_id(const std::filesystem::path& etc_root, const std::string& profile_id);

} // namespace btrfsbackup
