// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/BackupService.hpp>

#include <exception>
#include <memory>
#include <optional>
#include <vector>

#include <core/Errors.hpp>
#include <core/RuntimeTime.hpp>

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
    std::size_t actions_completed = 0,
    OperationKind operation_kind = OperationKind::Backup
) {
    events.on_backup_run_event(RunFailed{
        .profile_id = profile_id,
        .run_id = run_id,
        .error_code = error_code,
        .message = message,
        .operation_kind = operation_kind,
    });
    return {
        .profile_id = profile_id,
        .run_id = run_id,
        .error_code = error_code,
        .error_message = message,
        .actions_completed = actions_completed,
    };
}

std::optional<BackupExecutionFailed> close_target_or_fail(
    execution::RunExecutionContext& context,
    IBackupRunEventSink& events,
    const ProfileId& profile_id,
    const RunId& run_id,
    std::size_t actions_completed,
    OperationKind operation_kind
) {
    const std::optional<TargetCleanupError> cleanup_error = context.close_target_session();
    if (!cleanup_error.has_value()) {
        return std::nullopt;
    }
    return emit_run_failed(
        events,
        profile_id,
        run_id,
        ErrorCode::BackupFailed,
        cleanup_error->message,
        actions_completed,
        operation_kind
    );
}

void close_standalone_target_or_throw(IMountedTargetSession& target_session) {
    if (std::optional<TargetCleanupError> cleanup_error = target_session.close()) {
        throw CodedOperationError(ErrorCode::BackupFailed, cleanup_error->message);
    }
}

[[noreturn]] void rethrow_planning_failure_after_target_cleanup(
    IMountedTargetSession& target_session,
    const std::exception_ptr& original_error
) {
    const std::optional<TargetCleanupError> cleanup_error = target_session.close();
    try {
        std::rethrow_exception(original_error);
    } catch (const std::exception& error) {
        if (cleanup_error.has_value()) {
            throw CodedOperationError(
                failure_code(error),
                std::string(error.what()) + "; target cleanup also failed: " + cleanup_error->message
            );
        }
        throw;
    } catch (...) {
        if (cleanup_error.has_value()) {
            throw CodedOperationError(
                ErrorCode::BackupFailed,
                "unknown planning failure; target cleanup also failed: " + cleanup_error->message
            );
        }
        throw;
    }
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
    execution::RunSessionFactory& sessions,
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
    std::optional<BackupRunPlan> plan;
    try {
        const BackupPlanningSnapshot snapshot = discovery_.discover(loaded.profile, application_paths_, cancellation);
        plan = plan_builder_.build(loaded.profile, snapshot, run_id, time, cancellation);
    } catch (...) {
        const std::exception_ptr original_error = std::current_exception();
        rethrow_planning_failure_after_target_cleanup(*target_session, original_error);
    }
    close_standalone_target_or_throw(*target_session);
    return std::move(*plan);
}

BackupExecutionResult BackupService::start(const BackupRequest& request) {
    const RuntimeTimePoint started_at = clock_.now();
    const RunId run_id = run_ids_.generate(started_at);
    const execution::RunIdentity identity{run_id, started_at};
    const OperationKind operation_kind = request.validate_only
        ? OperationKind::TargetValidation
        : OperationKind::Backup;

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
            error.what(),
            0,
            operation_kind
        );
    }

    std::unique_ptr<IBackupRunEventSink> event_sink = sessions_.events(*loaded, identity);
    return start_loaded_profile(request, identity, operation_kind, *loaded, std::move(event_sink));
}

BackupService::RunLeaseResult BackupService::acquire_run_lease(
    const btrfsbackup::config::Profile& profile,
    const execution::RunIdentity& identity,
    OperationKind operation_kind,
    IBackupRunEventSink& events
) {
    BackupRunLeaseResult lease_result = sessions_.try_acquire_lease(profile);
    if (auto* busy = std::get_if<BackupRunLeaseBusy>(&lease_result)) {
        (void)emit_run_failed(
            events,
            profile.id,
            identity.run_id,
            busy->error_code,
            busy->error_message,
            0,
            operation_kind
        );
        return BackupExecutionBusy{
            .profile_id = profile.id,
            .run_id = identity.run_id,
            .error_code = busy->error_code,
            .error_message = std::move(busy->error_message),
        };
    }

    return std::move(std::get<BackupRunLeaseAcquired>(lease_result).lease);
}

std::optional<BackupExecutionResult> BackupService::finish_validation_if_requested(
    const BackupRequest& request,
    const btrfsbackup::config::Profile& profile,
    const execution::RunIdentity& identity,
    OperationKind operation_kind,
    BackupRunPlan& plan,
    execution::RunExecutionContext& context,
    IBackupRunEventSink& events
) {
    if (!request.validate_only) {
        return std::nullopt;
    }
    if (std::optional<BackupExecutionFailed> failed = close_target_or_fail(
            context,
            events,
            profile.id,
            identity.run_id,
            0,
            operation_kind
        )) {
        (void)context.close();
        return *failed;
    }
    if (context.cancellation_token().cancellation_requested()) {
        throw OperationCancelledError("backup cancelled during target cleanup");
    }
    events.on_backup_run_event(TargetValidationCompleted{profile.id, identity.run_id});
    return BackupExecutionValidated{std::move(plan)};
}

std::optional<BackupExecutionResult> BackupService::skip_if_daily_limit_reached(
    const BackupRequest& request,
    const btrfsbackup::config::LoadedProfile& loaded_profile,
    const execution::RunIdentity& identity,
    LocalDate today,
    OperationKind operation_kind,
    BackupRunPlan& plan,
    execution::RunExecutionContext& context,
    IBackupRunEventSink& events
) {
    const btrfsbackup::config::Profile& profile = loaded_profile.profile;
    if (request.force || !profile.settings.daily_limit ||
        !ledger_.last_success_matches(profile, today, loaded_profile.fingerprint)) {
        return std::nullopt;
    }
    if (std::optional<BackupExecutionFailed> failed = close_target_or_fail(
            context,
            events,
            profile.id,
            identity.run_id,
            0,
            operation_kind
        )) {
        (void)context.close();
        return *failed;
    }
    if (context.cancellation_token().cancellation_requested()) {
        throw OperationCancelledError("backup cancelled during target cleanup");
    }
    ledger_.write_skipped(profile, identity.run_id, identity.started_at, clock_.now(), plan.sources.size());
    return BackupExecutionSkipped{std::move(plan)};
}

BackupExecutionResult BackupService::execute_plan(
    const btrfsbackup::config::LoadedProfile& loaded_profile,
    const execution::RunIdentity& identity,
    LocalDate today,
    OperationKind operation_kind,
    BackupRunPlan plan,
    execution::RunExecutionContext& context,
    IBackupRunEventSink& events
) {
    const btrfsbackup::config::Profile& profile = loaded_profile.profile;
    BackupRunExecutionResult execution = run_factory_.execute(
        plan,
        events,
        context.checkpoint_store(),
        context.cancellation_token()
    );

    if (const auto* completed = std::get_if<BackupRunExecutionCompleted>(&execution)) {
        if (std::optional<BackupExecutionFailed> failed = close_target_or_fail(
                context,
                events,
                profile.id,
                identity.run_id,
                completed->actions_completed,
                operation_kind
            )) {
            (void)context.close();
            return *failed;
        }
        if (context.cancellation_token().cancellation_requested()) {
            throw OperationCancelledError("backup cancelled during target cleanup");
        }
        std::vector<BackupCompletionWarning> warnings;
        record_success_ledger_warning(warnings, loaded_profile, identity, today, plan.sources.size());
        record_terminal_status_warning(warnings, events, profile, identity);
        BackupExecutionResult result = BackupExecutionCompleted{
            std::move(plan),
            completed->actions_completed,
            std::move(warnings),
        };
        (void)context.close();
        return result;
    }
    if (const auto* failed = std::get_if<BackupRunExecutionFailed>(&execution)) {
        BackupExecutionResult result = BackupExecutionFailed{
            .profile_id = profile.id,
            .run_id = identity.run_id,
            .error_code = failed->error_code,
            .error_message = failed->error_message,
            .actions_completed = failed->actions_completed,
        };
        (void)context.close();
        return result;
    }
    BackupExecutionResult result = BackupExecutionCancelled{
        std::move(plan),
        std::get<BackupRunExecutionCancelled>(execution).actions_completed,
    };
    (void)context.close();
    return result;
}

void BackupService::record_success_ledger_warning(
    std::vector<BackupCompletionWarning>& warnings,
    const btrfsbackup::config::LoadedProfile& loaded_profile,
    const execution::RunIdentity& identity,
    LocalDate today,
    std::size_t source_count
) {
    try {
        ledger_.write_success(
            loaded_profile.profile,
            identity.run_id,
            today,
            clock_.now(),
            loaded_profile.fingerprint,
            source_count
        );
    } catch (const std::exception& error) {
        warnings.push_back({BackupCompletionWarningComponent::SuccessLedger, failure_code(error), error.what()});
    } catch (...) {
        warnings.push_back({
            BackupCompletionWarningComponent::SuccessLedger,
            ErrorCode::BackupFailed,
            "unknown completion metadata error",
        });
    }
}

void BackupService::record_terminal_status_warning(
    std::vector<BackupCompletionWarning>& warnings,
    IBackupRunEventSink& events,
    const btrfsbackup::config::Profile& profile,
    const execution::RunIdentity& identity
) {
    try {
        events.on_backup_run_event(RunCompleted{profile.id, identity.run_id});
    } catch (const std::exception& error) {
        warnings.push_back({BackupCompletionWarningComponent::TerminalStatus, failure_code(error), error.what()});
    } catch (...) {
        warnings.push_back({
            BackupCompletionWarningComponent::TerminalStatus,
            ErrorCode::BackupFailed,
            "unknown completion metadata error",
        });
    }
}

BackupExecutionResult BackupService::start_loaded_profile(
    const BackupRequest& request,
    const execution::RunIdentity& identity,
    OperationKind operation_kind,
    const btrfsbackup::config::LoadedProfile& loaded_profile,
    std::unique_ptr<IBackupRunEventSink> event_sink
) {
    const RunId& run_id = identity.run_id;
    const btrfsbackup::config::Profile& profile = loaded_profile.profile;
    IBackupRunEventSink& events = *event_sink;
    std::unique_ptr<execution::RunExecutionContext> context;
    try {
        RunLeaseResult lease_result = acquire_run_lease(
            profile,
            identity,
            operation_kind,
            events
        );
        if (auto* busy = std::get_if<BackupExecutionBusy>(&lease_result)) {
            return std::move(*busy);
        }
        context = sessions_.create_preparing(
            loaded_profile,
            identity,
            std::move(std::get<std::unique_ptr<IBackupRunLease>>(lease_result))
        );
        context->attach_event_sink(std::move(event_sink));
        events.on_backup_run_event(RunStarted{profile.id, run_id, operation_kind});

        BackupRunPlan plan = prepare_target_and_plan(profile, identity, *context);
        if (std::optional<BackupExecutionResult> validation = finish_validation_if_requested(
                request,
                profile,
                identity,
                operation_kind,
                plan,
                *context,
                events
            )) {
            return std::move(*validation);
        }

        const LocalDate today = clock_.local_date();
        if (std::optional<BackupExecutionResult> skipped = skip_if_daily_limit_reached(
                request,
                loaded_profile,
                identity,
                today,
                operation_kind,
                plan,
                *context,
                events
            )) {
            return std::move(*skipped);
        }
        return execute_plan(
            loaded_profile,
            identity,
            today,
            operation_kind,
            std::move(plan),
            *context,
            events
        );
    } catch (const OperationCancelledError& error) {
        events.on_backup_run_event(RunCancelled{
            .profile_id = profile.id,
            .run_id = run_id,
            .source_id = std::nullopt,
            .source_index = 0,
            .action_kind = std::nullopt,
            .error_code = ErrorCode::RunnerCancelled,
            .message = error.what(),
            .operation_kind = operation_kind,
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
            events,
            profile.id,
            run_id,
            failure_code(error),
            error.what(),
            0,
            operation_kind
        );
        if (context != nullptr) {
            (void)context->close();
        }
        return result;
    } catch (const std::exception& error) {
        (void)emit_run_failed(
            events,
            profile.id,
            run_id,
            ErrorCode::BackupFailed,
            error.what(),
            0,
            operation_kind
        );
        if (context != nullptr) {
            (void)context->close();
        }
        throw;
    }
}

BackupRunPlan BackupService::prepare_target_and_plan(
    const btrfsbackup::config::Profile& profile,
    const execution::RunIdentity& identity,
    execution::RunExecutionContext& context
) {
    std::unique_ptr<IMountedTargetSession> target_session = preflight_.run(
        profile,
        TargetMountMode::MountIfNeeded,
        context.cancellation_token()
    );
    context.attach_target_session(std::move(target_session));
    if (context.cancellation_token().cancellation_requested()) {
        throw OperationCancelledError("backup cancelled during preflight");
    }

    const BackupPlanningSnapshot snapshot = discovery_.discover(
        profile,
        application_paths_,
        context.cancellation_token()
    );
    BackupRunPlan plan = plan_builder_.build(
        profile,
        snapshot,
        identity.run_id,
        identity.started_at,
        context.cancellation_token()
    );
    if (context.cancellation_token().cancellation_requested()) {
        throw OperationCancelledError("backup cancelled during planning");
    }
    return plan;
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
