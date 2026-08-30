// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/BackupRunExecutor.hpp>

#include <cstdint>
#include <exception>
#include <string>

#include <core/Errors.hpp>

namespace btrfsbackup::backup {

namespace {

ErrorCode run_error_code(const std::exception& error) {
    if (const auto* coded_error = dynamic_cast<const CodedError*>(&error)) {
        return coded_error->error_code;
    }
    return ErrorCode::BackupFailed;
}

void emit_action_failure(
    IBackupRunEventSink& events,
    const BackupRunPlan& plan,
    const BackupSourceRunPlan& source,
    int source_index,
    BackupRunActionKind action_kind,
    const std::exception& error
) {
    events.on_backup_run_event(ActionFailed{
        .profile_id = plan.profile_id,
        .run_id = plan.run_id,
        .source_id = source.source_id,
        .source_index = source_index,
        .action_kind = action_kind,
        .error_code = run_error_code(error),
        .message = error.what(),
    });
}

int source_index_for_event(const BackupRunPlan& plan, const BackupSourceRunPlan& source) {
    for (std::size_t i = 0; i < plan.sources.size(); ++i) {
        if (plan.sources.at(i).source_id == source.source_id) {
            return static_cast<int>(i + 1);
        }
    }
    return 0;
}

void emit_cancelled(
    IBackupRunEventSink& events,
    const BackupRunPlan& plan,
    const BackupSourceRunPlan* source,
    std::optional<BackupRunActionKind> action_kind,
    std::optional<ErrorCode> error_code = std::nullopt,
    const std::string& message = ""
) {
    events.on_backup_run_event(RunCancelled{
        .profile_id = plan.profile_id,
        .run_id = plan.run_id,
        .source_id = source == nullptr ? std::nullopt : std::optional<SourceId>{source->source_id},
        .source_index = source == nullptr ? 0 : source_index_for_event(plan, *source),
        .action_kind = action_kind,
        .error_code = error_code,
        .message = message,
    });
}

} // namespace

BackupRunExecutor::BackupRunExecutor(
    IBackupActionExecutor& action_executor,
    IBackupRunCheckpointStore& checkpoints
)
    : action_executor_(action_executor), checkpoint_policy_(checkpoints) {
}

BackupRunExecutionResult BackupRunExecutor::execute(
    const BackupRunPlan& plan,
    IBackupRunEventSink& events,
    CancellationToken& cancellation
) {
    std::size_t actions_completed = 0;
    std::uint64_t completed_run_bytes = 0;
    for (const BackupSourceRunPlan& source : plan.sources) {
        if (cancellation.cancellation_requested()) {
            emit_cancelled(events, plan, &source, std::nullopt);
            return BackupRunExecutionCancelled{actions_completed};
        }

        const int source_index = source_index_for_event(plan, source);
        events.on_backup_run_event(SourceStarted{plan.profile_id, plan.run_id, source.source_id, source_index});

        for (const BackupRunAction& action : source.actions()) {
            const BackupRunActionKind action_kind = backup_run_action_kind(action);
            if (cancellation.cancellation_requested()) {
                emit_cancelled(events, plan, &source, std::nullopt);
                return BackupRunExecutionCancelled{actions_completed};
            }

            events.on_backup_run_event(ActionStarted{
                plan.profile_id,
                plan.run_id,
                source.source_id,
                source_index,
                action_kind,
            });
            try {
                BackupActionExecutionContext context{
                    .plan = plan,
                    .source = source,
                    .source_index = source_index,
                    .completed_run_bytes = completed_run_bytes,
                    .events = events,
                    .cancellation = cancellation,
                };
                const BackupActionExecutionResult action_result = action_executor_.execute(action, context);
                if (action_result.cancelled) {
                    emit_cancelled(events, plan, &source, action_kind);
                    return BackupRunExecutionCancelled{actions_completed};
                }
                completed_run_bytes += action_result.bytes_transferred;
            } catch (const OperationCancelledError& error) {
                emit_cancelled(
                    events,
                    plan,
                    &source,
                    action_kind,
                    ErrorCode::RunnerCancelled,
                    error.what()
                );
                return BackupRunExecutionCancelled{actions_completed};
            } catch (const BtrfsBackupError& error) {
                const ErrorCode error_code = run_error_code(error);
                emit_action_failure(events, plan, source, source_index, action_kind, error);
                events.on_backup_run_event(RunFailed{
                    .profile_id = plan.profile_id,
                    .run_id = plan.run_id,
                    .error_code = error_code,
                    .message = error.what(),
                });
                return BackupRunExecutionFailed{
                    .error_code = error_code,
                    .error_message = error.what(),
                    .actions_completed = actions_completed,
                };
            } catch (const std::exception& error) {
                emit_action_failure(events, plan, source, source_index, action_kind, error);
                throw;
            }

            ++actions_completed;
            events.on_backup_run_event(ActionCompleted{
                plan.profile_id,
                plan.run_id,
                source.source_id,
                source_index,
                action_kind,
            });

            checkpoint_policy_.after_success(action, plan, source, events);
        }

        events.on_backup_run_event(SourceCompleted{
            plan.profile_id,
            plan.run_id,
            source.source_id,
            source_index,
        });
    }

    return BackupRunExecutionCompleted{actions_completed};
}

} // namespace btrfsbackup::backup
