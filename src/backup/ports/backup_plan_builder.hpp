// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include <backup/model/backup_run_plan.hpp>
#include <backup/ports/backup_discovery.hpp>
#include <config/model/profile.hpp>
#include <core/identifiers.hpp>

namespace btrfsbackup::backup {

class IBackupPlanBuilder {
  public:
    virtual ~IBackupPlanBuilder() = default;

    [[nodiscard]] virtual BackupRunPlan build(
        const btrfsbackup::config::Profile& profile,
        const BackupPlanningSnapshot& snapshot,
        const RunId& run_id,
        const std::string& snapshot_timestamp
    ) const = 0;
};

} // namespace btrfsbackup::backup
