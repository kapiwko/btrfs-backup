// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <backup/model/BackupExecution.hpp>
#include <backup/model/BackupRunPlan.hpp>
#include <backup/ports/IBackupDiscovery.hpp>
#include <backup/ports/IBackupPreflight.hpp>
#include <backup/ports/IBackupPlanBuilder.hpp>
#include <backup/ports/IBackupRunFactory.hpp>
#include <backup/ports/RunContext.hpp>
#include <backup/ports/IRunLedger.hpp>
#include <backup/execution/RunSessionFactory.hpp>
#include <config/ApplicationPaths.hpp>
#include <config/ports/IProfileRepository.hpp>
#include <config/domain/Profile.hpp>
#include <core/Identifiers.hpp>

namespace btrfsbackup::backup {

class TargetStorageRecorder;

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
        execution::RunSessionFactory& sessions,
        IClock& clock,
        IRunIdGenerator& run_ids,
        TargetStorageRecorder* target_storage = nullptr
    );

    [[nodiscard]] BackupExecutionResult start(const BackupRequest& request);
    [[nodiscard]] BackupRunPlan plan(const BackupPlanRequest& request);
    [[nodiscard]] CancelBackupResult cancel(const CancellationRequest& request);

  private:
    using RunLeaseResult = std::variant<std::unique_ptr<IBackupRunLease>, BackupExecutionBusy>;

    [[nodiscard]] BackupExecutionResult start_loaded_profile(
        const BackupRequest& request,
        const execution::RunIdentity& identity,
        OperationKind operation_kind,
        const btrfsbackup::config::LoadedProfile& loaded_profile,
        std::unique_ptr<IBackupRunEventSink> event_sink
    );
    [[nodiscard]] BackupRunPlan prepare_target_and_plan(
        const btrfsbackup::config::Profile& profile,
        const execution::RunIdentity& identity,
        execution::RunExecutionContext& context
    );
    [[nodiscard]] RunLeaseResult acquire_run_lease(
        const btrfsbackup::config::Profile& profile,
        const execution::RunIdentity& identity,
        OperationKind operation_kind,
        IBackupRunEventSink& events
    );
    [[nodiscard]] std::optional<BackupExecutionResult> finish_validation_if_requested(
        const BackupRequest& request,
        const btrfsbackup::config::Profile& profile,
        const execution::RunIdentity& identity,
        OperationKind operation_kind,
        BackupRunPlan& plan,
        execution::RunExecutionContext& context,
        IBackupRunEventSink& events
    );
    [[nodiscard]] std::optional<BackupExecutionResult> skip_if_daily_limit_reached(
        const BackupRequest& request,
        const btrfsbackup::config::LoadedProfile& loaded_profile,
        const execution::RunIdentity& identity,
        LocalDate today,
        OperationKind operation_kind,
        BackupRunPlan& plan,
        execution::RunExecutionContext& context,
        IBackupRunEventSink& events
    );
    [[nodiscard]] BackupExecutionResult execute_plan(
        const btrfsbackup::config::LoadedProfile& loaded_profile,
        const execution::RunIdentity& identity,
        LocalDate today,
        OperationKind operation_kind,
        BackupRunPlan plan,
        execution::RunExecutionContext& context,
        IBackupRunEventSink& events
    );
    void record_success_ledger_warning(
        std::vector<BackupCompletionWarning>& warnings,
        const btrfsbackup::config::LoadedProfile& loaded_profile,
        const execution::RunIdentity& identity,
        LocalDate today,
        std::size_t source_count
    );
    static void record_terminal_status_warning(
        std::vector<BackupCompletionWarning>& warnings,
        IBackupRunEventSink& events,
        const btrfsbackup::config::Profile& profile,
        const execution::RunIdentity& identity
    );
    void record_target_storage(
        const btrfsbackup::config::Profile& profile,
        std::vector<BackupCompletionWarning>* warnings = nullptr
    );

    btrfsbackup::config::IProfileRepository& profiles_;
    btrfsbackup::config::ApplicationPaths application_paths_;
    IBackupPreflight& preflight_;
    IBackupDiscovery& discovery_;
    IBackupPlanBuilder& plan_builder_;
    IBackupRunFactory& run_factory_;
    IRunLedger& ledger_;
    execution::RunSessionFactory& sessions_;
    IClock& clock_;
    IRunIdGenerator& run_ids_;
    TargetStorageRecorder* target_storage_;
};

} // namespace btrfsbackup::backup
