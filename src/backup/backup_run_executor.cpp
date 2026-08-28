// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/backup_run_executor.hpp>

#include <cstdint>
#include <exception>
#include <string>
#include <type_traits>

#include <core/errors.hpp>

namespace btrfsbackup::backup {

void NullBackupRunEventSink::on_backup_run_event(const BackupRunEvent&) {
}

namespace {

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

class BackupTransferEventAdapter final : public btrfsbackup::backup::transfer::ITransferEventSink {
  public:
    BackupTransferEventAdapter(
        IBackupRunEventSink& events,
        const BackupRunPlan& plan,
        const BackupSourceRunPlan& source,
        std::uint64_t run_bytes_base
    )
        : events_(events),
          plan_(plan),
          source_(source),
          run_bytes_base_(run_bytes_base) {
    }

    void on_transfer_event(const btrfsbackup::backup::transfer::TransferEvent& event) override {
        if (event.kind == btrfsbackup::backup::transfer::TransferEventKind::Progress) {
            events_.on_backup_run_event(TransferProgress{
                .profile_id = plan_.profile_id,
                .run_id = plan_.run_id,
                .source_id = source_.source_id,
                .source_index = source_index_for_event(plan_, source_),
                .bytes_transferred = event.bytes_transferred,
                .bytes_produced = event.bytes_produced,
                .bytes_total_estimated = event.bytes_total_estimated,
                .run_bytes_transferred = run_bytes_base_ + event.bytes_transferred,
                .delta_bytes = event.delta_bytes,
                .elapsed_ms = event.elapsed_ms,
                .speed_bps = event.speed_bps,
                .message = event.message,
            });
        }
    }

  private:
    IBackupRunEventSink& events_;
    const BackupRunPlan& plan_;
    const BackupSourceRunPlan& source_;
    std::uint64_t run_bytes_base_ = 0;
};

} // namespace

BackupRunExecutor::BackupRunExecutor(
    IBackupRunActionHandler& action_handler,
    btrfsbackup::backup::transfer::IAsyncTransferPipeline& transfer_pipeline,
    IBackupRunCheckpointStore& checkpoints,
    const ISafeDirectoryRootFactory& safe_directories
)
    : action_handler_(action_handler),
      transfer_coordinator_(transfer_pipeline, safe_directories),
      checkpoint_policy_(checkpoints) {
}

BackupRunExecutionResult BackupRunExecutor::execute(
    const BackupRunPlan& plan,
    IBackupRunEventSink& events,
    CancellationToken& cancellation
) {
    BackupRunExecutionResult result;
    std::uint64_t completed_run_bytes = 0;
    events.on_backup_run_event(RunStarted{plan.profile_id, plan.run_id});

    for (const BackupSourceRunPlan& source : plan.sources) {
        if (cancellation.cancellation_requested()) {
            result.outcome = BackupRunExecutionOutcome::Cancelled;
            emit_cancelled(events, plan, &source, std::nullopt);
            return result;
        }

        const int source_index = source_index_for_event(plan, source);
        events.on_backup_run_event(SourceStarted{plan.profile_id, plan.run_id, source.source_id, source_index});

        for (const BackupRunAction& action : source.actions) {
            const BackupRunActionKind action_kind = backup_run_action_kind(action);
            if (cancellation.cancellation_requested()) {
                result.outcome = BackupRunExecutionOutcome::Cancelled;
                emit_cancelled(events, plan, &source, std::nullopt);
                return result;
            }

            events.on_backup_run_event(ActionStarted{
                plan.profile_id,
                plan.run_id,
                source.source_id,
                source_index,
                action_kind,
            });
            bool transfer_cancelled = false;
            try {
                std::visit([&](const auto& typed_action) {
                    using Action = std::decay_t<decltype(typed_action)>;
                    if constexpr (std::is_same_v<Action, SendReceiveAction>) {
                        BackupTransferEventAdapter transfer_events(
                            events,
                            plan,
                            source,
                            completed_run_bytes
                        );
                        btrfsbackup::backup::transfer::TransferResult transfer_result = transfer_coordinator_.execute(
                            typed_action,
                            plan.target_mount_point,
                            transfer_events,
                            cancellation
                        );
                        if (transfer_result.cancelled) {
                            transfer_cancelled = true;
                            return;
                        }
                        completed_run_bytes += transfer_result.bytes_transferred;
                    } else {
                        action_handler_.handle(action, plan, cancellation);
                    }
                },
                           action);
                if (transfer_cancelled) {
                    result.outcome = BackupRunExecutionOutcome::Cancelled;
                    emit_cancelled(events, plan, &source, action_kind);
                    return result;
                }
            } catch (const OperationCancelledError& error) {
                result.outcome = BackupRunExecutionOutcome::Cancelled;
                emit_cancelled(
                    events,
                    plan,
                    &source,
                    action_kind,
                    ErrorCode::RunnerCancelled,
                    error.what()
                );
                return result;
            } catch (const std::exception& error) {
                std::optional<ErrorCode> error_code;
                if (const auto* coded_error = dynamic_cast<const CodedError*>(&error)) {
                    error_code = coded_error->error_code;
                }
                events.on_backup_run_event(ActionFailed{
                    .profile_id = plan.profile_id,
                    .run_id = plan.run_id,
                    .source_id = source.source_id,
                    .source_index = source_index,
                    .action_kind = action_kind,
                    .error_code = error_code,
                    .message = error.what(),
                });
                throw;
            }

            ++result.actions_completed;
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

    events.on_backup_run_event(RunCompleted{plan.profile_id, plan.run_id});
    return result;
}

} // namespace btrfsbackup::backup
