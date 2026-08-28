// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/ports/backup_plan_builder.hpp>

namespace btrfsbackup::backup {

class BackupPlanBuilder final : public IBackupPlanBuilder {
  public:
    [[nodiscard]] BackupRunPlan build(
        const btrfsbackup::config::Profile& profile,
        const BackupDiscoveryResult& discovery,
        const RunId& run_id,
        const std::string& snapshot_timestamp
    ) const override;
};

} // namespace btrfsbackup::backup
