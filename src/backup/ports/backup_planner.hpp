// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

#include <backup/model/backup_run_plan.hpp>
#include <backup/ports/mount_inspector.hpp>
#include <config/application_paths.hpp>
#include <config/model/profile.hpp>
#include <core/identifiers.hpp>

namespace btrfsbackup::backup {

class IBackupPlanner {
  public:
    virtual ~IBackupPlanner() = default;

    [[nodiscard]] virtual BackupRunPlan build(
        const btrfsbackup::config::Profile& profile,
        const std::vector<MountEntry>& mounts,
        const btrfsbackup::config::ApplicationPaths& paths,
        const RunId& run_id,
        const std::string& snapshot_timestamp
    ) const = 0;
};

} // namespace btrfsbackup::backup
