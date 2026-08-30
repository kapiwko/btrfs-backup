// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>

#include <config/json/Json.hpp>

namespace btrfsbackup::config::json {

Json load_json_file(const std::filesystem::path& path);
std::string dump_json(const Json& data);

} // namespace btrfsbackup::config::json
