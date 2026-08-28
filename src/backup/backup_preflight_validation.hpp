// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include <backup/ports/mount_inspector.hpp>
#include <config/model/profile.hpp>

namespace btrfsbackup::backup {

void validate_backup_target_mount(const btrfsbackup::config::Profile& profile, const std::vector<MountEntry>& mounts);
void validate_backup_mounts(const btrfsbackup::config::Profile& profile, const std::vector<MountEntry>& mounts);

} // namespace btrfsbackup::backup
