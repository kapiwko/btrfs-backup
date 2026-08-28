// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include <backup/model/backup_planning_snapshot.hpp>
#include <backup/ports/mount_inspector.hpp>
#include <config/application_paths.hpp>
#include <config/model/profile.hpp>

namespace btrfsbackup::backup {

class IBackupDiscovery {
  public:
    virtual ~IBackupDiscovery() = default;

    [[nodiscard]] virtual BackupPlanningSnapshot discover(
        const btrfsbackup::config::Profile& profile,
        const std::vector<MountEntry>& mounts,
        const btrfsbackup::config::ApplicationPaths& paths
    ) const = 0;
};

} // namespace btrfsbackup::backup
