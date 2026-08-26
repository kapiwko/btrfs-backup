// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include <config/model/profile.hpp>

namespace btrfsbackup {

std::string render_udev(const Profile& profile);
std::string render_mount_requirement(const Profile& profile);
std::string render_mount_dependency(const Profile& profile);

} // namespace btrfsbackup
