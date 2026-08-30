// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <string>

#include <backup/model/BackupExecution.hpp>
#include <backup/model/BackupRunPlan.hpp>
#include <backup/ports/IBackupDiscovery.hpp>
#include <backup/ports/IBackupPreflight.hpp>
#include <backup/ports/IBackupPlanBuilder.hpp>
#include <backup/ports/IBackupRunFactory.hpp>
#include <backup/ports/RunContext.hpp>
#include <backup/ports/IRunLedger.hpp>
#include <backup/RunSessionFactory.hpp>
#include <config/ApplicationPaths.hpp>
#include <config/ports/IProfileRepository.hpp>
#include <config/model/Profile.hpp>
#include <core/Identifiers.hpp>

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
    [[nodiscard]] BackupExecutionResult start_loaded_profile(
        const BackupRequest& request,
        const RunIdentity& identity,
        OperationKind operation_kind,
        const btrfsbackup::config::LoadedProfile& loaded_profile,
        std::unique_ptr<IBackupRunEventSink> event_sink
    );
    [[nodiscard]] BackupRunPlan prepare_target_and_plan(
        const btrfsbackup::config::Profile& profile,
        const RunIdentity& identity,
        RunExecutionContext& context
    );

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
