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
    std::unique_ptr<IMountedTargetSession> target_session_value,
    ICheckpointStoreFactory& checkpoint_factory,
    ICancellationRequestStore& cancellation_requests,
    ICancellationMonitor& cancellation_monitor
)
    : profile_id(std::move(profile_id_value)),
      run_id(std::move(run_id_value)),
      checkpoints(checkpoint_factory.checkpoints(profile_id)),
      lease(std::move(lease_value)),
      target_session(std::move(target_session_value)) {
    active_run = cancellation_requests.register_active_run({profile_id, run_id});
    cancellation_watch = cancellation_monitor.watch({profile_id, run_id}, cancellation);
}

RunExecutionContext::~RunExecutionContext() {
    active_run.reset();
    cancellation_watch.reset();
}

} // namespace btrfsbackup::backup
