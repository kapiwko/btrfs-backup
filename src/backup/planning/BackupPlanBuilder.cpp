// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/planning/BackupPlanBuilder.hpp>

#include <core/Errors.hpp>

namespace btrfsbackup::backup::planning {

BackupRunPlan BackupPlanBuilder::build(
    const btrfsbackup::config::Profile& profile,
    const BackupPlanningSnapshot& snapshot,
    const RunId& run_id,
    RuntimeTimePoint snapshot_timestamp,
    CancellationToken& cancellation
) const {
    if (cancellation.cancellation_requested()) {
        throw OperationCancelledError("backup cancelled during planning");
    }
    BackupRunPlan plan = build_backup_run_plan(
        profile,
        snapshot.local_inventory(),
        snapshot.remote_inventory(),
        snapshot.pending_markers(),
        snapshot.pending_snapshots(),
        snapshot.profile_state_dir(),
        run_id,
        snapshot_timestamp
    );
    if (cancellation.cancellation_requested()) {
        throw OperationCancelledError("backup cancelled during planning");
    }
    return plan;
}

} // namespace btrfsbackup::backup::planning
