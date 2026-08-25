// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>

namespace btrfsbackup {

long long parse_uint(const std::string& value, const std::string& name, long long maximum = 1000000000000000LL);
long long parse_positive_uint(const std::string& value, const std::string& name, long long maximum = 1000000000000000LL);
std::filesystem::path normalized_path(const std::filesystem::path& path);
std::filesystem::path normalized_absolute_path(const std::string& value, const std::string& name);
std::filesystem::path normalized_relative_path(const std::string& value, const std::string& name);
bool path_is_within(const std::filesystem::path& candidate, const std::filesystem::path& base);

} // namespace btrfsbackup
