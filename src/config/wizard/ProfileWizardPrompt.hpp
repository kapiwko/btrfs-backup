// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>

namespace btrfsbackup::config::wizard {

std::string trim_text(const std::string& value);
bool parse_bool(const std::string& value);
std::uint64_t parse_uint(const std::string& value);

std::string prompt_value(std::istream& input, std::ostream& output, const std::string& label, const std::string& default_value);
bool prompt_bool(std::istream& input, std::ostream& output, const std::string& label, bool default_value);
std::uint64_t prompt_uint(std::istream& input, std::ostream& output, const std::string& label, std::uint64_t default_value);

} // namespace btrfsbackup::config::wizard
