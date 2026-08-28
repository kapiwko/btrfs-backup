// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/backup_service.hpp>
#include <backup/run_execution_context.hpp>

#include <utility>

#include <core/errors.hpp>

namespace btrfsbackup::backup {

namespace {

BackupRunStatusDescription status_description(
    const btrfsbackup::config::Profile& profile,
    const BackupRunPlan& plan,
    const std::string& started_at
) {
    std::map<std::string, std::string> source_names;
    for (const btrfsbackup::config::ProfileSource& source : profile.sources) {
        source_names.emplace(source.id.value(), source.name);
    }
    return {
        .profile_name = profile.name,
        .source_count = static_cast<int>(plan.sources.size()),
        .started_at = started_at,
        .source_names = std::move(source_names),
        .target_name = profile.target.mapper_name,
    };
}

} // namespace

BackupService::BackupService(
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
)
    : profiles_(profiles),
      application_paths_(std::move(application_paths)),
      mounts_(mounts),
      target_mounter_(target_mounter),
      planner_(planner),
      run_factory_(run_factory),
      leases_(leases),
      ledger_(ledger),
      event_sinks_(event_sinks),
      checkpoints_(checkpoints),
      cancellation_requests_(cancellation_requests),
      cancellation_monitor_(cancellation_monitor),
      clock_(clock),
      run_ids_(run_ids) {
}

BackupRunPlan BackupService::prepare_plan(
    const btrfsbackup::config::Profile& profile,
    const RunId& run_id,
    const std::string& timestamp
) {
    target_mounter_.ensure_mounted(profile);
    return planner_.build(profile, mounts_.inspect(), application_paths_, run_id, timestamp);
}

BackupRunPlan BackupService::plan(const BackupRequest& request) {
    const std::string timestamp = clock_.snapshot_timestamp();
    const RunId run_id = run_ids_.generate(timestamp);
    const btrfsbackup::config::LoadedProfile loaded = profiles_.get(request.profile_id);
    return prepare_plan(loaded.profile, run_id, timestamp);
}

BackupExecutionResult BackupService::start(const BackupRequest& request) {
    const std::string timestamp = clock_.snapshot_timestamp();
    const RunId run_id = run_ids_.generate(timestamp);
    const btrfsbackup::config::LoadedProfile loaded = profiles_.get(request.profile_id);
    const btrfsbackup::config::Profile& profile = loaded.profile;

    BackupRunLeaseResult lease_result = leases_.try_acquire(profile);
    if (auto* busy = std::get_if<BackupRunLeaseBusy>(&lease_result)) {
        return BackupExecutionBusy{
            .profile_id = profile.id,
            .run_id = run_id,
            .error_code = busy->error_code,
            .error_message = std::move(busy->error_message),
        };
    }
    std::unique_ptr<IBackupRunLease> lease = std::move(std::get<BackupRunLeaseAcquired>(lease_result).lease);

    BackupRunPlan plan = prepare_plan(profile, run_id, timestamp);
    const std::string& fingerprint = loaded.fingerprint.value();
    if (request.validate_only) {
        return BackupExecutionValidated{std::move(plan)};
    }

    const std::string today = clock_.local_date();
    if (!request.force && profile.settings.daily_limit && ledger_.last_success_matches(profile, today, fingerprint)) {
        ledger_.write_skipped(profile, run_id, timestamp, clock_.local_timestamp(), plan.sources.size());
        return BackupExecutionSkipped{std::move(plan)};
    }

    RunExecutionContext context(
        profile.id,
        run_id,
        std::move(lease),
        checkpoints_,
        event_sinks_,
        cancellation_requests_,
        cancellation_monitor_,
        status_description(profile, plan, timestamp)
    );
    BackupRunExecutionResult execution = run_factory_.execute(
        plan,
        *context.events,
        *context.checkpoints,
        context.cancellation
    );
    cancellation_requests_.clear_cancel_request({profile.id, run_id});

    if (const auto* completed = std::get_if<BackupRunExecutionCompleted>(&execution)) {
        ledger_.write_success(
            profile,
            run_id,
            today,
            clock_.local_timestamp(),
            fingerprint,
            plan.sources.size()
        );
        return BackupExecutionCompleted{std::move(plan), completed->actions_completed};
    }
    return BackupExecutionCancelled{
        std::move(plan),
        std::get<BackupRunExecutionCancelled>(execution).actions_completed,
    };
}

CancelBackupResult BackupService::cancel(const CancellationRequest& request) {
    const btrfsbackup::config::LoadedProfile loaded = profiles_.get(request.profile_id);
    const CancellationRequest validated_request{loaded.profile.id, request.run_id};
    BackupRunLeaseResult lease = leases_.try_acquire(loaded.profile);
    const CancellationRequestOutcome outcome = std::holds_alternative<BackupRunLeaseAcquired>(lease)
        ? CancellationRequestOutcome::StaleRun
        : cancellation_requests_.request_cancel(validated_request);
    switch (outcome) {
    case CancellationRequestOutcome::Accepted:
        return CancellationAccepted{loaded.profile.id, request.run_id};
    case CancellationRequestOutcome::StaleRun:
        return CancellationStaleRun{loaded.profile.id, request.run_id};
    case CancellationRequestOutcome::RunMismatch:
        return CancellationRunMismatch{loaded.profile.id, request.run_id};
    }
    throw BtrfsBackupError("unknown cancellation request outcome");
}

} // namespace btrfsbackup::backup
