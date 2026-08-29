// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/backup_service.hpp>
#include <backup/run_execution_context.hpp>

#include <algorithm>
#include <exception>
#include <memory>
#include <optional>
#include <utility>

#include <core/errors.hpp>
#include <core/runtime_time.hpp>

namespace btrfsbackup::backup {

namespace {

BackupRunStatusDescription status_description(
    const btrfsbackup::config::Profile& profile,
    RuntimeTimePoint started_at
) {
    std::map<std::string, std::string> source_names;
    for (const btrfsbackup::config::ProfileSource& source : profile.sources) {
        source_names.emplace(source.id.value(), source.name);
    }
    return {
        .profile_name = profile.name,
        .source_count = static_cast<int>(std::count_if(
            profile.sources.begin(),
            profile.sources.end(),
            [](const btrfsbackup::config::ProfileSource& source) { return source.enabled; }
        )),
        .started_at = started_at,
        .source_names = std::move(source_names),
        .target_name = profile.target.mapper_name.value(),
    };
}

BackupRunStatusDescription fallback_status_description(
    const ProfileId& profile_id,
    RuntimeTimePoint started_at
) {
    return {
        .profile_name = std::string(profile_id.value()),
        .source_count = 0,
        .started_at = started_at,
        .source_names = {},
        .target_name = {},
    };
}

ErrorCode failure_code(const std::exception& error) {
    if (const auto* coded_error = dynamic_cast<const CodedError*>(&error)) {
        return coded_error->error_code;
    }
    return ErrorCode::BackupFailed;
}

BackupExecutionFailed emit_run_failed(
    IBackupRunEventSink& events,
    const ProfileId& profile_id,
    const RunId& run_id,
    ErrorCode error_code,
    const std::string& message,
    std::size_t actions_completed = 0
) {
    events.on_backup_run_event(RunFailed{
        .profile_id = profile_id,
        .run_id = run_id,
        .error_code = error_code,
        .message = message,
    });
    return {
        .profile_id = profile_id,
        .run_id = run_id,
        .error_code = error_code,
        .error_message = message,
        .actions_completed = actions_completed,
    };
}

} // namespace

BackupService::BackupService(
    btrfsbackup::config::IProfileRepository& profiles,
    btrfsbackup::config::ApplicationPaths application_paths,
    IBackupPreflight& preflight,
    IBackupDiscovery& discovery,
    IBackupPlanBuilder& plan_builder,
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
      preflight_(preflight),
      discovery_(discovery),
      plan_builder_(plan_builder),
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

BackupRunPlan BackupService::build_plan(
    const btrfsbackup::config::Profile& profile,
    const RunId& run_id,
    const std::string& timestamp
) {
    const BackupPlanningSnapshot snapshot = discovery_.discover(profile, application_paths_);
    return plan_builder_.build(profile, snapshot, run_id, timestamp);
}

BackupRunPlan BackupService::plan(const BackupPlanRequest& request) {
    const RuntimeTimePoint time = clock_.now();
    const std::string timestamp = format_utc_snapshot_timestamp(time);
    const RunId run_id = run_ids_.generate(time);
    const btrfsbackup::config::LoadedProfile loaded = profiles_.get(request.profile_id);
    BackupRunLeaseResult lease_result = leases_.try_acquire(loaded.profile);
    if (auto* busy = std::get_if<BackupRunLeaseBusy>(&lease_result)) {
        throw CodedOperationError(busy->error_code, busy->error_message);
    }
    std::unique_ptr<IBackupRunLease> lease = std::move(std::get<BackupRunLeaseAcquired>(lease_result).lease);
    std::unique_ptr<IMountedTargetSession> target_session = preflight_.run(
        loaded.profile,
        request.mount_target ? TargetMountMode::MountIfNeeded : TargetMountMode::RequireMounted
    );
    return build_plan(loaded.profile, run_id, timestamp);
}

BackupExecutionResult BackupService::start(const BackupRequest& request) {
    const RuntimeTimePoint started_at = clock_.now();
    const std::string timestamp = format_utc_snapshot_timestamp(started_at);
    const RunId run_id = run_ids_.generate(started_at);

    std::optional<btrfsbackup::config::LoadedProfile> loaded;
    try {
        loaded = profiles_.get(request.profile_id);
    } catch (const BtrfsBackupError& error) {
        std::unique_ptr<IBackupRunEventSink> events = event_sinks_.events(
            fallback_status_description(request.profile_id, started_at)
        );
        return emit_run_failed(
            *events,
            request.profile_id,
            run_id,
            failure_code(error),
            error.what()
        );
    }

    const btrfsbackup::config::LoadedProfile& loaded_profile = *loaded;
    const btrfsbackup::config::Profile& profile = loaded_profile.profile;
    std::unique_ptr<IBackupRunEventSink> events = event_sinks_.events(
        status_description(profile, started_at)
    );
    try {
        BackupRunLeaseResult lease_result = leases_.try_acquire(profile);
        if (auto* busy = std::get_if<BackupRunLeaseBusy>(&lease_result)) {
            (void)emit_run_failed(
                *events,
                profile.id,
                run_id,
                busy->error_code,
                busy->error_message
            );
            return BackupExecutionBusy{
                .profile_id = profile.id,
                .run_id = run_id,
                .error_code = busy->error_code,
                .error_message = std::move(busy->error_message),
            };
        }
        std::unique_ptr<IBackupRunLease> lease = std::move(std::get<BackupRunLeaseAcquired>(lease_result).lease);
        events->on_backup_run_event(RunStarted{profile.id, run_id});

        std::unique_ptr<IMountedTargetSession> target_session = preflight_.run(
            profile,
            TargetMountMode::MountIfNeeded
        );
        BackupRunPlan plan = build_plan(profile, run_id, timestamp);
        const std::string& fingerprint = loaded_profile.fingerprint.value();
        if (request.validate_only) {
            events->on_backup_run_event(RunCompleted{profile.id, run_id});
            return BackupExecutionValidated{std::move(plan)};
        }

        const LocalDate today = clock_.local_date();
        if (!request.force && profile.settings.daily_limit && ledger_.last_success_matches(profile, today, fingerprint)) {
            ledger_.write_skipped(profile, run_id, started_at, clock_.now(), plan.sources.size());
            return BackupExecutionSkipped{std::move(plan)};
        }

        RunExecutionContext context(
            profile.id,
            run_id,
            std::move(lease),
            std::move(target_session),
            checkpoints_,
            cancellation_requests_,
            cancellation_monitor_
        );
        BackupRunExecutionResult execution = run_factory_.execute(
            plan,
            *events,
            *context.checkpoints,
            context.cancellation
        );
        cancellation_requests_.clear_cancel_request({profile.id, run_id});

        if (const auto* completed = std::get_if<BackupRunExecutionCompleted>(&execution)) {
            ledger_.write_success(
                profile,
                run_id,
                today,
                clock_.now(),
                fingerprint,
                plan.sources.size()
            );
            events->on_backup_run_event(RunCompleted{profile.id, run_id});
            return BackupExecutionCompleted{std::move(plan), completed->actions_completed};
        }
        if (const auto* failed = std::get_if<BackupRunExecutionFailed>(&execution)) {
            return BackupExecutionFailed{
                .profile_id = profile.id,
                .run_id = run_id,
                .error_code = failed->error_code,
                .error_message = failed->error_message,
                .actions_completed = failed->actions_completed,
            };
        }
        return BackupExecutionCancelled{
            std::move(plan),
            std::get<BackupRunExecutionCancelled>(execution).actions_completed,
        };
    } catch (const BtrfsBackupError& error) {
        return emit_run_failed(
            *events,
            profile.id,
            run_id,
            failure_code(error),
            error.what()
        );
    } catch (const std::exception& error) {
        (void)emit_run_failed(
            *events,
            profile.id,
            run_id,
            ErrorCode::BackupFailed,
            error.what()
        );
        throw;
    }
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
