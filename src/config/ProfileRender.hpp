// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <string>

#include <config/domain/Profile.hpp>

namespace btrfsbackup::config {

std::string render_udev(const Profile& profile);
std::string render_mount_requirement(const Profile& profile);
std::string render_mount_dependency(const Profile& profile);
std::string target_mount_unit_name(const std::filesystem::path& mount_point);
std::string render_target_mount_unit(const Profile& profile);

} // namespace btrfsbackup::config
