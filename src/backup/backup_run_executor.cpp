#include <backup/backup_run_executor.hpp>

#include <poll.h>

#include <cerrno>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <limits>
#include <string>

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
    const std::string& error_code = "",
    const std::string& message = ""
) {
    events.on_backup_run_event({
        .kind = kind,
        .profile_id = plan.profile_id,
        .run_id = plan.run_id,
        .source_id = source == nullptr ? std::string{} : source->source_id,
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
    std::uint64_t run_bytes_base_ = 0;
};

std::filesystem::path selected_parent_path(const BackupSourceRunPlan& source_plan) {
    if (source_plan.parent.local_parent.has_value()) {
        return source_plan.parent.local_parent->path;
    }
    return {};
}

TransferPipelinePlan transfer_plan_for_source(const BackupRunPlan& plan, const BackupSourceRunPlan& source_plan) {
    if (!plan.target_mount_point.empty()) {
        SafeDirectoryRoot local_root("/");
        SafeDirectoryRoot target_root(plan.target_mount_point);
        auto snapshot = std::make_shared<SafeDirectoryHandle>(local_root.open_directory(source_plan.local_snapshot_path));
        auto receive = std::make_shared<SafeDirectoryHandle>(target_root.open_directory(source_plan.incoming_run_dir));
        std::shared_ptr<SafeDirectoryHandle> parent;
        fs::path parent_path;
        if (source_plan.parent.local_parent.has_value()) {
            parent = std::make_shared<SafeDirectoryHandle>(
                local_root.open_directory(source_plan.parent.local_parent->path)
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
            .inherited_fds = {},
            .retained_handles = {},
        };
        transfer_plan.inherited_fds = {snapshot->fd(), receive->fd()};
        transfer_plan.retained_handles = {snapshot, receive};
        if (parent) {
            transfer_plan.inherited_fds.push_back(parent->fd());
            transfer_plan.retained_handles.push_back(parent);
        }
        return transfer_plan;
    }

    SendReceiveCommandPlan command_plan = build_send_receive_command_plan(
        source_plan.local_snapshot_path,
        selected_parent_path(source_plan),
        source_plan.incoming_run_dir
    );

    return TransferPipelinePlan{
        .producer_argv = command_plan.send_argv,
        .consumer_argv = command_plan.receive_argv,
        .bytes_total_estimated = 0,
        .inherited_fds = {},
        .retained_handles = {},
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
    std::uint64_t completed_run_bytes = 0;
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
                    action_effects_.execute_action(action, source, plan, cancellation);
                    BackupTransferEventAdapter transfer_events(events, plan, source, action.kind, completed_run_bytes);
                    TransferPipelinePlan transfer_plan = transfer_plan_for_source(plan, source);
                    transfer_plan.bytes_total_estimated = estimate_regular_file_bytes(
                        fs::path(transfer_plan.producer_argv.back())
                    );
                    std::unique_ptr<IAsyncTransferHandle> transfer = transfer_pipeline_.start(
                        transfer_plan,
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
                    completed_run_bytes += transfer_result.bytes_transferred;
                } else if (action_has_external_effect(action.kind)) {
                    action_effects_.execute_action(action, source, plan, cancellation);
                }
            } catch (const OperationCancelledError& error) {
                result.cancelled = true;
                emit_event(events, BackupRunEventKind::RunCancelled, plan, &source, action.kind, 0, 0, 0, 0, 0, 0, 0, "runner.cancelled", error.what());
                return result;
            } catch (const std::exception& error) {
                if (const auto* coded_error = dynamic_cast<const CodedValidationError*>(&error)) {
                    error_code = coded_error->error_code;
                }
                emit_event(events, BackupRunEventKind::ActionFailed, plan, &source, action.kind, 0, 0, 0, 0, 0, 0, 0, error_code, error.what());
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
