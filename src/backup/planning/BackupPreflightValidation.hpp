// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include <backup/ports/IMountInspector.hpp>
#include <config/domain/Profile.hpp>

namespace btrfsbackup::backup::planning {

MountEntry validate_backup_target_mount(
    const btrfsbackup::config::Profile& profile,
    const std::vector<MountEntry>& mounts
);
MountEntry validate_backup_mounts(
    const btrfsbackup::config::Profile& profile,
    const std::vector<MountEntry>& mounts
);

} // namespace btrfsbackup::backup::planning
