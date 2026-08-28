// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/run_execution_context.hpp>

#include <utility>

namespace btrfsbackup::backup {

RunExecutionContext::RunExecutionContext(
    ProfileId profile_id_value,
    RunId run_id_value,
    std::unique_ptr<IBackupRunLease> lease_value,
    ICheckpointStoreFactory& checkpoint_factory,
    IRunEventSinkFactory& event_sink_factory,
    ICancellationMonitor& cancellation_monitor,
    BackupRunStatusDescription status
)
    : profile_id(std::move(profile_id_value)),
      run_id(std::move(run_id_value)),
      cancellation_watch(cancellation_monitor.watch(profile_id, cancellation)),
      checkpoints(checkpoint_factory.checkpoints(profile_id)),
      events(event_sink_factory.events(std::move(status))),
      lease(std::move(lease_value)) {
}

} // namespace btrfsbackup::backup
