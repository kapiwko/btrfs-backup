// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>

namespace btrfsbackup {

unsigned long long available_bytes(const std::filesystem::path& path);
void check_minimum_free_space(const std::filesystem::path& path, unsigned long long minimum_bytes, const std::string& label);

} // namespace btrfsbackup
