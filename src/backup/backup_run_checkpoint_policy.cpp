// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/backup_run_checkpoint_policy.hpp>

#include <cstddef>

namespace btrfsbackup::backup {

namespace {

int source_index(const BackupRunPlan& plan, const SourceId& source_id) {
    for (std::size_t index = 0; index < plan.sources.size(); ++index) {
        if (plan.sources.at(index).source_id == source_id) {
            return static_cast<int>(index + 1);
        }
    }
    return 0;
}

} // namespace

BackupRunCheckpointPolicy::BackupRunCheckpointPolicy(IBackupRunCheckpointStore& checkpoints)
    : checkpoints_(checkpoints) {
}

void BackupRunCheckpointPolicy::after_success(
    const BackupRunAction& action,
    const BackupRunPlan& plan,
    const BackupSourceRunPlan& source,
    IBackupRunEventSink& events
) {
    const BackupRunActionKind action_kind = backup_run_action_kind(action);
    checkpoints_.write_checkpoint({
        .profile_id = plan.profile_id,
        .run_id = plan.run_id,
        .source_id = source.source_id,
        .action_kind = action_kind,
    });
    events.on_backup_run_event({
        .kind = BackupRunEventKind::CheckpointWritten,
        .profile_id = plan.profile_id,
        .run_id = plan.run_id,
        .source_id = source.source_id,
        .source_index = source_index(plan, source.source_id),
        .action_kind = action_kind,
        .bytes_transferred = 0,
        .bytes_produced = 0,
        .bytes_total_estimated = 0,
        .run_bytes_transferred = 0,
        .delta_bytes = 0,
        .elapsed_ms = 0,
        .speed_bps = 0,
        .error_code = std::nullopt,
        .message = {},
    });
}

} // namespace btrfsbackup::backup
