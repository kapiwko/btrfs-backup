// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace btrfsbackup::platform::linux {

// Linux Btrfs source discovery for the profile wizard.

std::string source_name_from_path(const std::string& path);
std::vector<std::string> detect_btrfs_sources();
std::string default_source_selection(const std::vector<std::string>& candidates);
std::vector<std::string> selected_sources_from_input(const std::vector<std::string>& candidates, const std::string& selection);
std::vector<std::string> select_sources(std::istream& input, std::ostream& output);

} // namespace btrfsbackup::platform::linux
