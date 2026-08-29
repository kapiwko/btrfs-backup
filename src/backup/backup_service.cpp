// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/backup_service.hpp>

#include <exception>
#include <memory>
#include <optional>
#include <utility>

#include <core/errors.hpp>
#include <core/runtime_time.hpp>

namespace btrfsbackup::backup {

namespace {

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
    IRunLedger& ledger,
    RunSessionFactory& sessions,
    IClock& clock,
    IRunIdGenerator& run_ids
)
    : profiles_(profiles),
      application_paths_(std::move(application_paths)),
      preflight_(preflight),
      discovery_(discovery),
      plan_builder_(plan_builder),
      run_factory_(run_factory),
      ledger_(ledger),
      sessions_(sessions),
      clock_(clock),
      run_ids_(run_ids) {
}

BackupRunPlan BackupService::plan(const BackupPlanRequest& request) {
    const RuntimeTimePoint time = clock_.now();
    const std::string timestamp = format_utc_snapshot_timestamp(time);
    const RunId run_id = run_ids_.generate(time);
    const btrfsbackup::config::LoadedProfile loaded = profiles_.get(request.profile_id);
    BackupRunLeaseResult lease_result = sessions_.try_acquire_lease(loaded.profile);
    if (auto* busy = std::get_if<BackupRunLeaseBusy>(&lease_result)) {
        throw CodedOperationError(busy->error_code, busy->error_message);
    }
    std::unique_ptr<IBackupRunLease> lease = std::move(std::get<BackupRunLeaseAcquired>(lease_result).lease);
    CancellationToken cancellation;
    std::unique_ptr<IMountedTargetSession> target_session = preflight_.run(
        loaded.profile,
        request.mount_target ? TargetMountMode::MountIfNeeded : TargetMountMode::RequireMounted,
        cancellation
    );
    const BackupPlanningSnapshot snapshot = discovery_.discover(loaded.profile, application_paths_, cancellation);
    return plan_builder_.build(loaded.profile, snapshot, run_id, timestamp, cancellation);
}

BackupExecutionResult BackupService::start(const BackupRequest& request) {
    const RuntimeTimePoint started_at = clock_.now();
    const std::string timestamp = format_utc_snapshot_timestamp(started_at);
    const RunId run_id = run_ids_.generate(started_at);
    const RunIdentity identity{run_id, started_at};

    std::optional<btrfsbackup::config::LoadedProfile> loaded;
    try {
        loaded = profiles_.get(request.profile_id);
    } catch (const BtrfsBackupError& error) {
        std::unique_ptr<IBackupRunEventSink> events = sessions_.fallback_events(request.profile_id, identity);
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
    std::unique_ptr<IBackupRunEventSink> events = sessions_.events(loaded_profile, identity);
    std::unique_ptr<RunExecutionContext> context;
    try {
        BackupRunLeaseResult lease_result = sessions_.try_acquire_lease(profile);
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
        context = sessions_.create_preparing(
            loaded_profile,
            identity,
            events,
            std::move(lease)
        );
        events->on_backup_run_event(RunStarted{profile.id, run_id});

        std::unique_ptr<IMountedTargetSession> target_session = preflight_.run(
            profile,
            TargetMountMode::MountIfNeeded,
            context->cancellation
        );
        context->attach_target_session(std::move(target_session));
        if (context->cancellation.cancellation_requested()) {
            throw OperationCancelledError("backup cancelled during preflight");
        }
        const BackupPlanningSnapshot snapshot = discovery_.discover(
            profile,
            application_paths_,
            context->cancellation
        );
        BackupRunPlan plan = plan_builder_.build(
            profile,
            snapshot,
            run_id,
            timestamp,
            context->cancellation
        );
        if (context->cancellation.cancellation_requested()) {
            throw OperationCancelledError("backup cancelled during planning");
        }
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

        BackupRunExecutionResult execution = run_factory_.execute(
            plan,
            *events,
            *context->checkpoints,
            context->cancellation
        );

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
            BackupExecutionResult result = BackupExecutionCompleted{std::move(plan), completed->actions_completed};
            (void)context->close();
            return result;
        }
        if (const auto* failed = std::get_if<BackupRunExecutionFailed>(&execution)) {
            BackupExecutionResult result = BackupExecutionFailed{
                .profile_id = profile.id,
                .run_id = run_id,
                .error_code = failed->error_code,
                .error_message = failed->error_message,
                .actions_completed = failed->actions_completed,
            };
            (void)context->close();
            return result;
        }
        BackupExecutionResult result = BackupExecutionCancelled{
            std::move(plan),
            std::get<BackupRunExecutionCancelled>(execution).actions_completed,
        };
        (void)context->close();
        return result;
    } catch (const OperationCancelledError& error) {
        events->on_backup_run_event(RunCancelled{
            .profile_id = profile.id,
            .run_id = run_id,
            .source_id = std::nullopt,
            .source_index = 0,
            .action_kind = std::nullopt,
            .error_code = ErrorCode::RunnerCancelled,
            .message = error.what(),
        });
        BackupExecutionResult result = BackupExecutionCancelled{
            BackupRunPlan{
                .profile_id = profile.id,
                .run_id = run_id,
                .target_mount_point = profile.target.mount_point,
                .sources = {},
            },
            0,
        };
        if (context != nullptr) {
            (void)context->close();
        }
        return result;
    } catch (const BtrfsBackupError& error) {
        BackupExecutionResult result = emit_run_failed(
            *events,
            profile.id,
            run_id,
            failure_code(error),
            error.what()
        );
        if (context != nullptr) {
            (void)context->close();
        }
        return result;
    } catch (const std::exception& error) {
        (void)emit_run_failed(
            *events,
            profile.id,
            run_id,
            ErrorCode::BackupFailed,
            error.what()
        );
        if (context != nullptr) {
            (void)context->close();
        }
        throw;
    }
}

CancelBackupResult BackupService::cancel(const CancellationRequest& request) {
    const btrfsbackup::config::LoadedProfile loaded = profiles_.get(request.profile_id);
    const CancellationRequest validated_request{loaded.profile.id, request.run_id};
    const CancellationRequestOutcome outcome = sessions_.request_cancel(loaded.profile, validated_request);
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
