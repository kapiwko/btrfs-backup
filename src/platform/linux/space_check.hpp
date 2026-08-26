// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace btrfsbackup {

std::uint64_t available_bytes(const std::filesystem::path& path);
void check_minimum_free_space(const std::filesystem::path& path, std::uint64_t minimum_bytes, const std::string& label);

} // namespace btrfsbackup
