#include <btrfsbackup/backup_run_executor.hpp>

#include <poll.h>

#include <cerrno>
#include <exception>
#include <filesystem>
#include <string>

#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/snapshot_transfer.hpp>

namespace btrfsbackup {

void NullBackupRunEventSink::on_backup_run_event(const BackupRunEvent&) {
}

namespace {

void emit_event(
    IBackupRunEventSink& events,
    BackupRunEventKind kind,
    const BackupRunPlan& plan,
    const BackupSourceRunPlan* source,
    BackupRunActionKind action_kind,
    std::uint64_t bytes_transferred = 0,
    std::uint64_t bytes_produced = 0,
    std::uint64_t delta_bytes = 0,
    std::uint64_t pending_bytes = 0,
    std::uint64_t elapsed_ms = 0,
    std::uint64_t speed_bps = 0,
    const std::string& error_code = "",
    const std::string& message = ""
) {
    events.on_backup_run_event({
        .kind = kind,
        .profile_id = plan.profile_id,
        .run_id = plan.run_id,
        .source_id = source == nullptr ? std::string{} : source->source_id,
        .action_kind = action_kind,
        .bytes_transferred = bytes_transferred,
        .bytes_produced = bytes_produced,
        .delta_bytes = delta_bytes,
        .pending_bytes = pending_bytes,
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
        BackupRunActionKind action_kind
    )
        : events_(events),
          plan_(plan),
          source_(source),
          action_kind_(action_kind) {
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
                event.delta_bytes,
                event.pending_bytes,
                event.elapsed_ms,
                event.speed_bps,
                "",
                event.message
            );
        }
    }

private:
    IBackupRunEventSink& events_;
    const BackupRunPlan& plan_;
    const BackupSourceRunPlan& source_;
    BackupRunActionKind action_kind_;
};

std::filesystem::path selected_parent_path(const BackupSourceRunPlan& source_plan) {
    if (source_plan.parent.local_parent.has_value()) {
        return source_plan.parent.local_parent->path;
    }
    return {};
}

TransferPipelinePlan transfer_plan_for_source(const BackupSourceRunPlan& source_plan) {
    SendReceiveCommandPlan command_plan = build_send_receive_command_plan(
        source_plan.local_snapshot_path,
        selected_parent_path(source_plan),
        source_plan.incoming_run_dir
    );

    return TransferPipelinePlan{
        .producer_argv = command_plan.send_argv,
        .consumer_argv = command_plan.receive_argv,
    };
}

bool action_has_external_effect(BackupRunActionKind kind) {
    return kind != BackupRunActionKind::SelectParent
        && kind != BackupRunActionKind::SendReceive;
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
    while (!transfer.finished()) {
        pollfd fds[2]{
            {.fd = transfer.completion_fd(), .events = POLLIN | POLLHUP, .revents = 0},
            {.fd = cancellation.cancellation_fd(), .events = POLLIN, .revents = 0},
        };
        int ready = poll(fds, 2, -1);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw ValidationError("transfer wait failed");
        }
        if ((fds[1].revents & POLLIN) != 0 || cancellation.cancellation_requested()) {
            cancellation.drain_cancellation_signal();
            transfer.request_cancel();
        }
    }
}

} // namespace

BackupRunExecutor::BackupRunExecutor(
    IBackupRunActionEffects& action_effects,
    IAsyncTransferPipeline& transfer_pipeline,
    IBackupRunCheckpointStore& checkpoints
)
    : action_effects_(action_effects),
      transfer_pipeline_(transfer_pipeline),
      checkpoints_(checkpoints) {
}

BackupRunExecutionResult BackupRunExecutor::execute(
    const BackupRunPlan& plan,
    IBackupRunEventSink& events,
    CancellationToken& cancellation
) {
    BackupRunExecutionResult result;
    emit_event(events, BackupRunEventKind::RunStarted, plan, nullptr, BackupRunActionKind::CleanupSource);

    for (const BackupSourceRunPlan& source : plan.sources) {
        if (cancellation.cancellation_requested()) {
            result.cancelled = true;
            emit_event(events, BackupRunEventKind::RunCancelled, plan, &source, BackupRunActionKind::CleanupSource);
            return result;
        }

        emit_event(events, BackupRunEventKind::SourceStarted, plan, &source, BackupRunActionKind::CleanupSource);

        for (const BackupRunAction& action : source.actions) {
            if (cancellation.cancellation_requested()) {
                result.cancelled = true;
                emit_event(events, BackupRunEventKind::RunCancelled, plan, &source, action.kind);
                return result;
            }

            emit_event(events, BackupRunEventKind::ActionStarted, plan, &source, action.kind);
            std::string error_code;
            try {
                if (action.kind == BackupRunActionKind::SendReceive) {
                    action_effects_.execute_action(action, source, plan);
                    BackupTransferEventAdapter transfer_events(events, plan, source, action.kind);
                    std::unique_ptr<IAsyncTransferHandle> transfer = transfer_pipeline_.start(
                        transfer_plan_for_source(source),
                        transfer_events
                    );
                    wait_for_transfer_or_cancellation(*transfer, cancellation);
                    TransferResult transfer_result = transfer->wait();
                    if (transfer_result.cancelled) {
                        result.cancelled = true;
                        emit_event(events, BackupRunEventKind::RunCancelled, plan, &source, action.kind);
                        return result;
                    }
                    error_code = transfer_failure_error_code(transfer_result);
                    require_transfer_success(transfer_result);
                } else if (action_has_external_effect(action.kind)) {
                    action_effects_.execute_action(action, source, plan);
                }
            } catch (const std::exception& error) {
                emit_event(events, BackupRunEventKind::ActionFailed, plan, &source, action.kind, 0, 0, 0, 0, 0, 0, error_code, error.what());
                throw;
            }

            ++result.actions_completed;
            emit_event(events, BackupRunEventKind::ActionCompleted, plan, &source, action.kind);

            if (backup_run_action_writes_checkpoint(action.kind)) {
                write_checkpoint(checkpoints_, events, plan, source, action.kind);
            }
        }

        emit_event(events, BackupRunEventKind::SourceCompleted, plan, &source, BackupRunActionKind::CleanupSource);
    }

    result.completed = true;
    emit_event(events, BackupRunEventKind::RunCompleted, plan, nullptr, BackupRunActionKind::CleanupSource);
    return result;
}

bool backup_run_action_writes_checkpoint(BackupRunActionKind kind) {
    return kind != BackupRunActionKind::SelectParent;
}

} // namespace btrfsbackup
