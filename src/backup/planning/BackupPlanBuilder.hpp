// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <backup/ports/IBackupPlanBuilder.hpp>

namespace btrfsbackup::backup::planning {

class BackupPlanBuilder final : public IBackupPlanBuilder {
  public:
    [[nodiscard]] BackupRunPlan build(
        const btrfsbackup::config::Profile& profile,
        const BackupPlanningSnapshot& snapshot,
        const RunId& run_id,
        RuntimeTimePoint snapshot_timestamp,
        CancellationToken& cancellation
    ) const override;
};

} // namespace btrfsbackup::backup::planning
