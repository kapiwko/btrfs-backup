// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>

#include <config/profile.hpp>

namespace btrfsbackup {

void render_wizard_tree(const Profile& profile, const std::string& keyfile, const std::filesystem::path& output_dir);
void apply_rendered_wizard_tree(const Profile& profile, const std::filesystem::path& output_dir);

} // namespace btrfsbackup
