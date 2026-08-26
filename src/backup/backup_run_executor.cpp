// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/backup_run_executor.hpp>

#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <string>
#include <type_traits>

#include <config/errors.hpp>
#include <platform/linux/safe_directory_root.hpp>
#include <backup/snapshot_transfer.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

void NullBackupRunEventSink::on_backup_run_event(const BackupRunEvent&) {
}

namespace {

int source_index_for_event(const BackupRunPlan& plan, const BackupSourceRunPlan* source) {
    if (source == nullptr) {
        return 0;
    }
    for (std::size_t i = 0; i < plan.sources.size(); ++i) {
        if (plan.sources.at(i).source_id == source->source_id) {
            return static_cast<int>(i + 1);
        }
    }
    return 0;
}

void emit_event(
    IBackupRunEventSink& events,
    BackupRunEventKind kind,
    const BackupRunPlan& plan,
    const BackupSourceRunPlan* source,
    BackupRunActionKind action_kind,
    std::uint64_t bytes_transferred = 0,
    std::uint64_t bytes_produced = 0,
    std::uint64_t bytes_total_estimated = 0,
    std::uint64_t run_bytes_transferred = 0,
    std::uint64_t delta_bytes = 0,
    std::uint64_t elapsed_ms = 0,
    std::uint64_t speed_bps = 0,
    std::optional<ErrorCode> error_code = std::nullopt,
    const std::string& message = ""
) {
    events.on_backup_run_event({
        .kind = kind,
        .profile_id = plan.profile_id,
        .run_id = plan.run_id,
        .source_id = source == nullptr
            ? std::nullopt
            : std::optional<SourceId>{source->source_id},
        .source_index = source_index_for_event(plan, source),
        .action_kind = action_kind,
        .bytes_transferred = bytes_transferred,
        .bytes_produced = bytes_produced,
        .bytes_total_estimated = bytes_total_estimated,
        .run_bytes_transferred = run_bytes_transferred,
        .delta_bytes = delta_bytes,
        .elapsed_ms = elapsed_ms,
        .speed_bps = speed_bps,
        .error_code = error_code,
        .message = message,
    });
}

class BackupTransferEventAdapter final : public ITransferEventSink {
public:
    BackupTransferEventAdapter(
        IBackupRunEventSink& events,
        const BackupRunPlan& plan,
        const BackupSourceRunPlan& source,
        BackupRunActionKind action_kind,
        std::uint64_t run_bytes_base
    )
        : events_(events),
          plan_(plan),
          source_(source),
          action_kind_(action_kind),
          run_bytes_base_(run_bytes_base) {
    }

    void on_transfer_event(const TransferEvent& event) override {
        if (event.kind == TransferEventKind::Progress) {
            emit_event(
                events_,
                BackupRunEventKind::TransferProgress,
                plan_,
                &source_,
                action_kind_,
                event.bytes_transferred,
                event.bytes_produced,
                event.bytes_total_estimated,
                run_bytes_base_ + event.bytes_transferred,
                event.delta_bytes,
                event.elapsed_ms,
                event.speed_bps,
                std::nullopt,
                event.message
            );
        }
    }

private:
    IBackupRunEventSink& events_;
    const BackupRunPlan& plan_;
    const BackupSourceRunPlan& source_;
    BackupRunActionKind action_kind_;
    std::uint64_t run_bytes_base_ = 0;
};

TransferPipelinePlan transfer_plan_for_action(const BackupRunPlan& plan, const SendReceiveAction& action) {
    if (!plan.target_mount_point.empty()) {
        SafeDirectoryRoot local_root("/");
        SafeDirectoryRoot target_root(plan.target_mount_point);
        auto snapshot = std::make_shared<SafeDirectoryHandle>(local_root.open_directory(action.snapshot));
        auto receive = std::make_shared<SafeDirectoryHandle>(target_root.open_directory(action.incoming_run_directory));
        std::shared_ptr<SafeDirectoryHandle> parent;
        fs::path parent_path;
        if (action.parent.has_value()) {
            parent = std::make_shared<SafeDirectoryHandle>(
                local_root.open_directory(*action.parent)
            );
            parent_path = parent->proc_path();
        }
        SendReceiveCommandPlan command_plan = build_send_receive_command_plan(
            snapshot->proc_path(),
            parent_path,
            receive->proc_path()
        );
        TransferPipelinePlan transfer_plan{
            .producer_argv = command_plan.send_argv,
            .consumer_argv = command_plan.receive_argv,
            .retained_resources = {},
        };
        transfer_plan.retained_resources = {snapshot, receive};
        if (parent) {
            transfer_plan.retained_resources.push_back(parent);
        }
        return transfer_plan;
    }

    SendReceiveCommandPlan command_plan = build_send_receive_command_plan(
        action.snapshot,
        action.parent.value_or(fs::path{}),
        action.incoming_run_directory
    );

    return TransferPipelinePlan{
        .producer_argv = command_plan.send_argv,
        .consumer_argv = command_plan.receive_argv,
        .bytes_total_estimated = 0,
        .retained_resources = {},
    };
}

std::uint64_t estimate_regular_file_bytes(const std::filesystem::path& root) {
    std::error_code ec;
    if (!std::filesystem::exists(root, ec) || ec) {
        return 0;
    }

    std::uint64_t total = 0;
    std::filesystem::recursive_directory_iterator iterator(
        root,
        std::filesystem::directory_options::skip_permission_denied,
        ec
    );
    std::filesystem::recursive_directory_iterator end;
    while (!ec && iterator != end) {
        std::error_code entry_ec;
        if (iterator->is_regular_file(entry_ec) && !entry_ec) {
            std::uintmax_t size = iterator->file_size(entry_ec);
            if (!entry_ec) {
                if (size > std::numeric_limits<std::uint64_t>::max() - total) {
                    return 0;
                }
                total += static_cast<std::uint64_t>(size);
            }
        }
        iterator.increment(ec);
    }

    return ec ? 0 : total;
}

void write_checkpoint(
    IBackupRunCheckpointStore& checkpoints,
    IBackupRunEventSink& events,
    const BackupRunPlan& plan,
    const BackupSourceRunPlan& source,
    BackupRunActionKind action_kind
) {
    checkpoints.write_checkpoint({
        .profile_id = plan.profile_id,
        .run_id = plan.run_id,
        .source_id = source.source_id,
        .action_kind = action_kind,
    });
    emit_event(events, BackupRunEventKind::CheckpointWritten, plan, &source, action_kind);
}

void wait_for_transfer_or_cancellation(IAsyncTransferHandle& transfer, CancellationToken& cancellation) {
    while (!transfer.wait_for(std::chrono::milliseconds(100))) {
        if (cancellation.cancellation_requested()) {
            transfer.request_cancel();
        }
    }
}

} // namespace

BackupRunExecutor::BackupRunExecutor(
    IBackupRunActionHandler& action_handler,
    IAsyncTransferPipeline& transfer_pipeline,
    IBackupRunCheckpointStore& checkpoints
)
    : action_handler_(action_handler),
      transfer_pipeline_(transfer_pipeline),
      checkpoints_(checkpoints) {
}

BackupRunExecutionResult BackupRunExecutor::execute(
    const BackupRunPlan& plan,
    IBackupRunEventSink& events,
    CancellationToken& cancellation
) {
    BackupRunExecutionResult result;
    std::uint64_t completed_run_bytes = 0;
    emit_event(events, BackupRunEventKind::RunStarted, plan, nullptr, BackupRunActionKind::CleanupSource);

    for (const BackupSourceRunPlan& source : plan.sources) {
        if (cancellation.cancellation_requested()) {
            result.outcome = BackupRunExecutionOutcome::Cancelled;
            emit_event(events, BackupRunEventKind::RunCancelled, plan, &source, BackupRunActionKind::CleanupSource);
            return result;
        }

        emit_event(events, BackupRunEventKind::SourceStarted, plan, &source, BackupRunActionKind::CleanupSource);

        for (const BackupRunAction& action : source.actions) {
            const BackupRunActionKind action_kind = backup_run_action_kind(action);
            if (cancellation.cancellation_requested()) {
                result.outcome = BackupRunExecutionOutcome::Cancelled;
                emit_event(events, BackupRunEventKind::RunCancelled, plan, &source, action_kind);
                return result;
            }

            emit_event(events, BackupRunEventKind::ActionStarted, plan, &source, action_kind);
            std::optional<ErrorCode> error_code;
            bool transfer_cancelled = false;
            try {
                std::visit([&](const auto& typed_action) {
                    using Action = std::decay_t<decltype(typed_action)>;
                    if constexpr (std::is_same_v<Action, SendReceiveAction>) {
                        action_handler_.handle(action, plan, cancellation);
                        BackupTransferEventAdapter transfer_events(
                            events,
                            plan,
                            source,
                            action_kind,
                            completed_run_bytes
                        );
                        TransferPipelinePlan transfer_plan = transfer_plan_for_action(plan, typed_action);
                        transfer_plan.bytes_total_estimated = estimate_regular_file_bytes(typed_action.snapshot);
                        std::unique_ptr<IAsyncTransferHandle> transfer = transfer_pipeline_.start(
                            transfer_plan,
                            transfer_events
                        );
                        wait_for_transfer_or_cancellation(*transfer, cancellation);
                        TransferResult transfer_result = transfer->wait();
                        if (transfer_result.cancelled) {
                            transfer_cancelled = true;
                            return;
                        }
                        error_code = transfer_failure_error_code(transfer_result);
                        require_transfer_success(transfer_result);
                        completed_run_bytes += transfer_result.bytes_transferred;
                    } else if constexpr (!std::is_same_v<Action, SelectParentAction>) {
                        action_handler_.handle(action, plan, cancellation);
                    }
                },
                           action);
                if (transfer_cancelled) {
                    result.outcome = BackupRunExecutionOutcome::Cancelled;
                    emit_event(events, BackupRunEventKind::RunCancelled, plan, &source, action_kind);
                    return result;
                }
            } catch (const OperationCancelledError& error) {
                result.outcome = BackupRunExecutionOutcome::Cancelled;
                emit_event(events, BackupRunEventKind::RunCancelled, plan, &source, action_kind, 0, 0, 0, 0, 0, 0, 0, ErrorCode::RunnerCancelled, error.what());
                return result;
            } catch (const std::exception& error) {
                if (const auto* coded_error = dynamic_cast<const CodedError*>(&error)) {
                    error_code = error_code_from_name(coded_error->error_code).value_or(ErrorCode::RunnerActionFailed);
                }
                emit_event(events, BackupRunEventKind::ActionFailed, plan, &source, action_kind, 0, 0, 0, 0, 0, 0, 0, error_code, error.what());
                throw;
            }

            ++result.actions_completed;
            emit_event(events, BackupRunEventKind::ActionCompleted, plan, &source, action_kind);

            if (backup_run_action_writes_checkpoint(action)) {
                write_checkpoint(checkpoints_, events, plan, source, action_kind);
            }
        }

        emit_event(events, BackupRunEventKind::SourceCompleted, plan, &source, BackupRunActionKind::CleanupSource);
    }

    emit_event(events, BackupRunEventKind::RunCompleted, plan, nullptr, BackupRunActionKind::CleanupSource);
    return result;
}

bool backup_run_action_writes_checkpoint(const BackupRunAction& action) {
    return !std::holds_alternative<SelectParentAction>(action);
}

} // namespace btrfsbackup
