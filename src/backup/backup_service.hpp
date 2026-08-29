// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

#include <backup/model/backup_execution.hpp>
#include <backup/model/backup_run_plan.hpp>
#include <backup/ports/backup_discovery.hpp>
#include <backup/ports/backup_preflight.hpp>
#include <backup/ports/backup_plan_builder.hpp>
#include <backup/ports/backup_run_factory.hpp>
#include <backup/ports/run_context.hpp>
#include <backup/ports/run_ledger.hpp>
#include <backup/run_session_factory.hpp>
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
        IBackupPreflight& preflight,
        IBackupDiscovery& discovery,
        IBackupPlanBuilder& plan_builder,
        IBackupRunFactory& run_factory,
        IRunLedger& ledger,
        RunSessionFactory& sessions,
        IClock& clock,
        IRunIdGenerator& run_ids
    );

    [[nodiscard]] BackupExecutionResult start(const BackupRequest& request);
    [[nodiscard]] BackupRunPlan plan(const BackupPlanRequest& request);
    [[nodiscard]] CancelBackupResult cancel(const CancellationRequest& request);

  private:
    [[nodiscard]] BackupRunPlan build_plan(const btrfsbackup::config::Profile& profile, const RunId& run_id, const std::string& timestamp);

    btrfsbackup::config::IProfileRepository& profiles_;
    btrfsbackup::config::ApplicationPaths application_paths_;
    IBackupPreflight& preflight_;
    IBackupDiscovery& discovery_;
    IBackupPlanBuilder& plan_builder_;
    IBackupRunFactory& run_factory_;
    IRunLedger& ledger_;
    RunSessionFactory& sessions_;
    IClock& clock_;
    IRunIdGenerator& run_ids_;
};

} // namespace btrfsbackup::backup
