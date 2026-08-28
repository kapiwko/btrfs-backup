// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include <backup/model/backup_execution.hpp>
#include <backup/model/backup_run_plan.hpp>
#include <backup/ports/backup_planner.hpp>
#include <backup/ports/backup_run_factory.hpp>
#include <backup/ports/cancellation_request_store.hpp>
#include <backup/ports/cancellation_monitor.hpp>
#include <backup/ports/checkpoint_store_factory.hpp>
#include <backup/ports/mount_inspector.hpp>
#include <backup/ports/run_context.hpp>
#include <backup/ports/run_event_sink_factory.hpp>
#include <backup/ports/run_ledger.hpp>
#include <backup/ports/run_lease.hpp>
#include <backup/ports/target_manager.hpp>
#include <config/application_paths.hpp>
#include <config/ports/profile_repository.hpp>
#include <config/model/profile.hpp>
#include <core/identifiers.hpp>

namespace btrfsbackup::backup {

class BackupService {
  public:
    BackupService(
        btrfsbackup::config::IProfileRepository& profiles,
        btrfsbackup::config::ApplicationPaths application_paths,
        IMountInspector& mounts,
        ITargetManager& target_mounter,
        IBackupPlanner& planner,
        IBackupRunFactory& run_factory,
        IBackupRunLeaseProvider& leases,
        IRunLedger& ledger,
        IRunEventSinkFactory& event_sinks,
        ICheckpointStoreFactory& checkpoints,
        ICancellationRequestStore& cancellation_requests,
        ICancellationMonitor& cancellation_monitor,
        IClock& clock,
        IRunIdGenerator& run_ids
    );

    [[nodiscard]] BackupExecutionResult start(const BackupRequest& request);
    [[nodiscard]] BackupRunPlan plan(const BackupRequest& request);
    [[nodiscard]] CancelBackupResult cancel(const ProfileId& profile_id);

  private:
    [[nodiscard]] BackupRunPlan prepare_plan(const btrfsbackup::config::Profile& profile, const RunId& run_id, const std::string& timestamp);

    btrfsbackup::config::IProfileRepository& profiles_;
    btrfsbackup::config::ApplicationPaths application_paths_;
    IMountInspector& mounts_;
    ITargetManager& target_mounter_;
    IBackupPlanner& planner_;
    IBackupRunFactory& run_factory_;
    IBackupRunLeaseProvider& leases_;
    IRunLedger& ledger_;
    IRunEventSinkFactory& event_sinks_;
    ICheckpointStoreFactory& checkpoints_;
    ICancellationRequestStore& cancellation_requests_;
    ICancellationMonitor& cancellation_monitor_;
    IClock& clock_;
    IRunIdGenerator& run_ids_;
};

} // namespace btrfsbackup::backup
