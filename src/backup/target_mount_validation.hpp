// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include <platform/linux/mount_info.hpp>
#include <config/profile.hpp>

namespace btrfsbackup {

void validate_target_mount(const Profile& profile, const std::vector<MountEntry>& mounts);

} // namespace btrfsbackup
