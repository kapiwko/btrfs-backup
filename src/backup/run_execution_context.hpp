// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>

#include <backup/ports/cancellation_monitor.hpp>
#include <backup/ports/run_lease.hpp>
#include <backup/ports/run_state_repository.hpp>
#include <core/cancellation.hpp>
#include <core/identifiers.hpp>

namespace btrfsbackup::backup {

struct RunExecutionContext {
    RunExecutionContext(
        ProfileId profile_id,
        RunId run_id,
        std::unique_ptr<IBackupRunLease> lease,
        IRunStateRepository& state,
        ICancellationMonitor& cancellation_monitor,
        BackupRunStatusDescription status
    );

    RunExecutionContext(const RunExecutionContext&) = delete;
    RunExecutionContext& operator=(const RunExecutionContext&) = delete;

    ProfileId profile_id;
    RunId run_id;
    CancellationToken cancellation;
    std::unique_ptr<ICancellationWatch> cancellation_watch;
    std::unique_ptr<IBackupRunCheckpointStore> checkpoints;
    std::unique_ptr<IBackupRunEventSink> events;
    std::unique_ptr<IBackupRunLease> lease;
};

} // namespace btrfsbackup::backup
