// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>

#include <backup/ports/CancellationMonitor.hpp>
#include <backup/ports/CancellationRequestStore.hpp>
#include <backup/ports/ICheckpointStoreFactory.hpp>
#include <backup/ports/IRunEventSinkFactory.hpp>
#include <backup/ports/RunLease.hpp>
#include <backup/ports/TargetManager.hpp>
#include <backup/RunExecutionContext.hpp>
#include <config/ConfigurationIdentity.hpp>
#include <core/RuntimeTime.hpp>

namespace btrfsbackup::backup {

struct RunIdentity {
    RunId run_id;
    RuntimeTimePoint started_at;
};

class RunSessionFactory {
  public:
    RunSessionFactory(
        IBackupRunLeaseProvider& leases,
        IRunEventSinkFactory& event_sinks,
        ICheckpointStoreFactory& checkpoints,
        ICancellationRequestStore& cancellation_requests,
        ICancellationMonitor& cancellation_monitor
    );

    [[nodiscard]] BackupRunLeaseResult try_acquire_lease(
        const btrfsbackup::config::Profile& profile
    );

    [[nodiscard]] CancellationRequestOutcome request_cancel(
        const btrfsbackup::config::Profile& profile,
        const CancellationRequest& request
    );

    [[nodiscard]] std::unique_ptr<IBackupRunEventSink> events(
        const btrfsbackup::config::LoadedProfile& loaded,
        const RunIdentity& identity
    );

    [[nodiscard]] std::unique_ptr<IBackupRunEventSink> fallback_events(
        const ProfileId& profile_id,
        const RunIdentity& identity
    );

    [[nodiscard]] std::unique_ptr<RunExecutionContext> create_preparing(
        const btrfsbackup::config::LoadedProfile& loaded,
        const RunIdentity& identity,
        std::unique_ptr<IBackupRunLease> lease
    );

  private:
    IBackupRunLeaseProvider& leases_;
    IRunEventSinkFactory& event_sinks_;
    ICheckpointStoreFactory& checkpoints_;
    ICancellationRequestStore& cancellation_requests_;
    ICancellationMonitor& cancellation_monitor_;
};

} // namespace btrfsbackup::backup
