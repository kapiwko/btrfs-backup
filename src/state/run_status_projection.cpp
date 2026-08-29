// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/run_status_projection.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <limits>
#include <string>
#include <state/serialization.hpp>
#include <state/run_history.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::state {

namespace {

struct EventProjectionData {
    btrfsbackup::backup::BackupRunEventKind kind;
    ProfileId profile_id;
    RunId run_id;
    std::optional<SourceId> source_id;
    int source_index = 0;
    std::optional<btrfsbackup::backup::BackupRunActionKind> action_kind;
    std::uint64_t bytes_transferred = 0;
    std::uint64_t bytes_produced = 0;
    std::uint64_t bytes_total_estimated = 0;
    std::uint64_t run_bytes_transferred = 0;
    std::uint64_t delta_bytes = 0;
    std::uint64_t elapsed_ms = 0;
    std::uint64_t speed_bps = 0;
    std::optional<ErrorCode> error_code;
    std::string message;
};

EventProjectionData event_projection_data(const btrfsbackup::backup::BackupRunEvent& event) {
    EventProjectionData data{
        .kind = btrfsbackup::backup::backup_run_event_kind(event),
        .profile_id = btrfsbackup::backup::backup_run_event_profile_id(event),
        .run_id = btrfsbackup::backup::backup_run_event_run_id(event),
        .source_id = btrfsbackup::backup::backup_run_event_source_id(event),
        .source_index = btrfsbackup::backup::backup_run_event_source_index(event),
        .action_kind = btrfsbackup::backup::backup_run_event_action_kind(event),
        .bytes_transferred = 0,
        .bytes_produced = 0,
        .bytes_total_estimated = 0,
        .run_bytes_transferred = 0,
        .delta_bytes = 0,
        .elapsed_ms = 0,
        .speed_bps = 0,
        .error_code = std::nullopt,
        .message = {},
    };
    if (const auto* progress = std::get_if<btrfsbackup::backup::TransferProgress>(&event)) {
        data.bytes_transferred = progress->bytes_transferred;
        data.bytes_produced = progress->bytes_produced;
        data.bytes_total_estimated = progress->bytes_total_estimated;
        data.run_bytes_transferred = progress->run_bytes_transferred;
        data.delta_bytes = progress->delta_bytes;
        data.elapsed_ms = progress->elapsed_ms;
        data.speed_bps = progress->speed_bps;
        data.message = progress->message;
    } else if (const auto* failed = std::get_if<btrfsbackup::backup::ActionFailed>(&event)) {
        data.error_code = failed->error_code;
        data.message = failed->message;
    } else if (const auto* failed = std::get_if<btrfsbackup::backup::RunFailed>(&event)) {
        data.error_code = failed->error_code;
        data.message = failed->message;
    } else if (const auto* cancelled = std::get_if<btrfsbackup::backup::RunCancelled>(&event)) {
        data.error_code = cancelled->error_code;
        data.message = cancelled->message;
    }
    return data;
}

RunPhase phase_for_action(btrfsbackup::backup::BackupRunActionKind kind) {
    switch (kind) {
    case btrfsbackup::backup::BackupRunActionKind::RecoverPending:
        return RunPhase::RecoverPending;
    case btrfsbackup::backup::BackupRunActionKind::CleanupIncoming:
        return RunPhase::CleanupIncoming;
    case btrfsbackup::backup::BackupRunActionKind::BeforeSnapshotHook:
        return RunPhase::BeforeSnapshotHook;
    case btrfsbackup::backup::BackupRunActionKind::CreateSnapshot:
        return RunPhase::CreateSnapshot;
    case btrfsbackup::backup::BackupRunActionKind::AfterSnapshotHook:
        return RunPhase::AfterSnapshotHook;
    case btrfsbackup::backup::BackupRunActionKind::SendReceive:
        return RunPhase::SendReceive;
    case btrfsbackup::backup::BackupRunActionKind::VerifyReceived:
        return RunPhase::VerifyReceived;
    case btrfsbackup::backup::BackupRunActionKind::CommitReceived:
        return RunPhase::CommitReceived;
    case btrfsbackup::backup::BackupRunActionKind::ApplyRemoteRetention:
        return RunPhase::ApplyRemoteRetention;
    case btrfsbackup::backup::BackupRunActionKind::ApplyLocalRetention:
        return RunPhase::ApplyLocalRetention;
    case btrfsbackup::backup::BackupRunActionKind::CleanupSource:
        return RunPhase::CleanupSource;
    }
    return RunPhase::RunStarted;
}

RunPhase phase_for_event(btrfsbackup::backup::BackupRunEventKind kind) {
    switch (kind) {
    case btrfsbackup::backup::BackupRunEventKind::RunStarted:
        return RunPhase::RunStarted;
    case btrfsbackup::backup::BackupRunEventKind::SourceStarted:
        return RunPhase::SourceStarted;
    case btrfsbackup::backup::BackupRunEventKind::TransferProgress:
        return RunPhase::Transferring;
    case btrfsbackup::backup::BackupRunEventKind::SourceCompleted:
        return RunPhase::SourceCompleted;
    case btrfsbackup::backup::BackupRunEventKind::RunCompleted:
        return RunPhase::Succeeded;
    case btrfsbackup::backup::BackupRunEventKind::RunFailed:
        return RunPhase::Failed;
    case btrfsbackup::backup::BackupRunEventKind::RunCancelled:
        return RunPhase::Cancelled;
    case btrfsbackup::backup::BackupRunEventKind::ActionStarted:
    case btrfsbackup::backup::BackupRunEventKind::ActionCompleted:
    case btrfsbackup::backup::BackupRunEventKind::ActionFailed:
    case btrfsbackup::backup::BackupRunEventKind::CheckpointWritten:
        return RunPhase::RunStarted;
    }
    return RunPhase::RunStarted;
}

bool failed_action_is_recoverable(btrfsbackup::backup::BackupRunActionKind kind) {
    return kind == btrfsbackup::backup::BackupRunActionKind::BeforeSnapshotHook || kind == btrfsbackup::backup::BackupRunActionKind::AfterSnapshotHook;
}

std::string suggested_action_for_failed_action(btrfsbackup::backup::BackupRunActionKind kind) {
    if (kind == btrfsbackup::backup::BackupRunActionKind::BeforeSnapshotHook || kind == btrfsbackup::backup::BackupRunActionKind::AfterSnapshotHook) {
        return "inspect-hook-program";
    }
    return "inspect-run-history";
}

int estimated_overall_progress(
    const BackupRunStatusContext& context,
    const EventProjectionData& event,
    int source_progress
) {
    if (context.source_count <= 0) {
        return -1;
    }
    if (event.kind == btrfsbackup::backup::BackupRunEventKind::RunCompleted) {
        return 100;
    }
    if (event.kind == btrfsbackup::backup::BackupRunEventKind::SourceCompleted && event.source_index > 0) {
        const std::int64_t completed_sources = std::clamp(event.source_index, 0, context.source_count);
        return static_cast<int>(completed_sources * 100 / context.source_count);
    }
    if (event.source_id.has_value() && event.source_index > 0) {
        const std::int64_t completed_sources = std::clamp(event.source_index - 1, 0, context.source_count);
        const int current_source_progress = std::clamp(source_progress, 0, 100);
        return static_cast<int>(
            (completed_sources * 100 + (source_progress >= 0 ? current_source_progress : 0)) / context.source_count
        );
    }
    return 0;
}

int estimated_source_progress(const EventProjectionData& event) {
    if (event.bytes_total_estimated == 0) {
        return -1;
    }
    if (event.bytes_transferred >= event.bytes_total_estimated) {
        return 100;
    }
    return static_cast<int>(
        static_cast<long double>(event.bytes_transferred) * 100.0L / static_cast<long double>(event.bytes_total_estimated)
    );
}

int estimated_eta_seconds(const EventProjectionData& event) {
    if (event.bytes_total_estimated == 0 || event.speed_bps == 0 || event.bytes_transferred >= event.bytes_total_estimated) {
        return -1;
    }
    std::uint64_t remaining = event.bytes_total_estimated - event.bytes_transferred;
    const std::uint64_t seconds = remaining / event.speed_bps + (remaining % event.speed_bps == 0 ? 0 : 1);
    return seconds > static_cast<std::uint64_t>(std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::max()
        : static_cast<int>(seconds);
}

std::string action_name_for_event(const EventProjectionData& event) {
    return event.action_kind.has_value()
        ? backup_run_action_kind_name(*event.action_kind)
        : "unknown";
}

std::string message_for_event(const EventProjectionData& event) {
    if (!event.message.empty()) {
        return event.message;
    }
    switch (event.kind) {
    case btrfsbackup::backup::BackupRunEventKind::RunStarted:
        return "Backup run started.";
    case btrfsbackup::backup::BackupRunEventKind::SourceStarted:
        return "Backup source started.";
    case btrfsbackup::backup::BackupRunEventKind::ActionStarted:
        return "Backup action started: " + action_name_for_event(event) + ".";
    case btrfsbackup::backup::BackupRunEventKind::TransferProgress:
        return "Backup transfer is running.";
    case btrfsbackup::backup::BackupRunEventKind::ActionCompleted:
        return "Backup action completed: " + action_name_for_event(event) + ".";
    case btrfsbackup::backup::BackupRunEventKind::ActionFailed:
        return "Backup action failed: " + action_name_for_event(event) + ".";
    case btrfsbackup::backup::BackupRunEventKind::CheckpointWritten:
        return "Backup checkpoint written.";
    case btrfsbackup::backup::BackupRunEventKind::SourceCompleted:
        return "Backup source completed.";
    case btrfsbackup::backup::BackupRunEventKind::RunCompleted:
        return "Backup completed.";
    case btrfsbackup::backup::BackupRunEventKind::RunFailed:
        return "Backup failed.";
    case btrfsbackup::backup::BackupRunEventKind::RunCancelled:
        return "Backup cancelled.";
    }
    return "Backup event.";
}

RunStatus status_for_event(
    const BackupRunStatusContext& context,
    const EventProjectionData& event,
    int minimum_overall_progress
) {
    const std::string source_id = event.source_id.has_value()
        ? std::string(event.source_id->value())
        : std::string{};
    auto source_name = context.source_names.find(source_id);
    RunState state = RunState::Running;
    RunPhase phase = phase_for_event(event.kind);
    const RuntimeTimePoint updated_at = std::chrono::system_clock::now();
    std::optional<RuntimeTimePoint> finished_at;
    int exit_code = 0;

    if ((event.kind == btrfsbackup::backup::BackupRunEventKind::ActionStarted || event.kind == btrfsbackup::backup::BackupRunEventKind::ActionCompleted || event.kind == btrfsbackup::backup::BackupRunEventKind::ActionFailed || event.kind == btrfsbackup::backup::BackupRunEventKind::RunFailed || event.kind == btrfsbackup::backup::BackupRunEventKind::CheckpointWritten) && event.action_kind.has_value()) {
        phase = phase_for_action(*event.action_kind);
    } else if (event.kind == btrfsbackup::backup::BackupRunEventKind::TransferProgress) {
        phase = RunPhase::Transferring;
    }

    if (event.kind == btrfsbackup::backup::BackupRunEventKind::RunFailed) {
        state = RunState::Failed;
        exit_code = 1;
        finished_at = updated_at;
    } else if (event.kind == btrfsbackup::backup::BackupRunEventKind::RunCompleted) {
        state = RunState::Succeeded;
        phase = RunPhase::Succeeded;
        finished_at = updated_at;
    } else if (event.kind == btrfsbackup::backup::BackupRunEventKind::RunCancelled) {
        state = RunState::Cancelled;
        phase = RunPhase::Cancelled;
        exit_code = 130;
        finished_at = updated_at;
    }

    RunDetails details;
    std::optional<ErrorCode> error_code;
    std::string error_message;
    bool recoverable = false;
    std::string suggested_action;
    bool can_cancel = false;
    std::uint64_t bytes_processed = 0;
    std::uint64_t bytes_total_estimated = 0;
    std::uint64_t run_bytes_processed = 0;
    std::uint64_t speed_bps = 0;
    int eta_seconds = -1;
    int source_progress = event.kind == btrfsbackup::backup::BackupRunEventKind::TransferProgress
        ? estimated_source_progress(event)
        : -1;
    int overall_progress = estimated_overall_progress(context, event, source_progress);
    if (minimum_overall_progress >= 0 && event.kind != btrfsbackup::backup::BackupRunEventKind::RunStarted) {
        overall_progress = std::max(overall_progress, minimum_overall_progress);
    }
    std::string progress_accuracy = overall_progress >= 0 ? "estimated" : "indeterminate";
    if (event.kind == btrfsbackup::backup::BackupRunEventKind::TransferProgress) {
        can_cancel = true;
        bytes_processed = event.bytes_transferred;
        bytes_total_estimated = event.bytes_total_estimated;
        run_bytes_processed = event.run_bytes_transferred;
        speed_bps = event.speed_bps;
        eta_seconds = estimated_eta_seconds(event);
        progress_accuracy = source_progress >= 0 ? "estimated" : progress_accuracy;
        details = {
            {"bytesProduced", event.bytes_produced},
            {"bytesTransferred", event.bytes_transferred},
            {"bytesTotalEstimated", event.bytes_total_estimated},
            {"deltaBytes", event.delta_bytes},
            {"elapsedMs", event.elapsed_ms},
            {"speedBps", event.speed_bps}
        };
    }
    if (run_bytes_processed == 0) {
        run_bytes_processed = bytes_processed;
    }
    if (event.kind == btrfsbackup::backup::BackupRunEventKind::ActionFailed) {
        details = {
            {"sourceId", source_id},
            {"action", event.action_kind.has_value() ? backup_run_action_kind_name(*event.action_kind) : ""}
        };
    } else if (event.kind == btrfsbackup::backup::BackupRunEventKind::RunFailed) {
        error_code = event.error_code.value_or(ErrorCode::BackupFailed);
        error_message = event.message;
        details = {
            {"sourceId", source_id},
            {"action", event.action_kind.has_value() ? backup_run_action_kind_name(*event.action_kind) : ""}
        };
        if (error_code == ErrorCode::RepositoryRecoveryRequired) {
            recoverable = true;
            suggested_action = "run-backup-recovery";
        } else if (event.action_kind.has_value()) {
            recoverable = failed_action_is_recoverable(*event.action_kind);
            suggested_action = event.action_kind.has_value()
                ? suggested_action_for_failed_action(*event.action_kind)
                : "inspect-run-history";
        } else {
            recoverable = true;
            suggested_action = *error_code == ErrorCode::ConfigurationChanged ||
                    *error_code == ErrorCode::RunnerProfileBusy ||
                    *error_code == ErrorCode::RunnerTargetBusy
                ? "run-backup-again"
                : "inspect-run-history";
        }
    } else if (event.kind == btrfsbackup::backup::BackupRunEventKind::RunCancelled) {
        error_code = ErrorCode::RunnerCancelled;
        error_message = message_for_event(event);
        details = {
            {"sourceId", source_id},
            {"action", event.action_kind.has_value() ? backup_run_action_kind_name(*event.action_kind) : ""}
        };
        recoverable = true;
        suggested_action = "run-backup-again";
    }

    std::optional<RunError> error;
    if (error_code.has_value()) {
        error = RunError{
            .code = *error_code,
            .message = error_message,
            .recoverable = recoverable,
            .suggested_action = SuggestedAction{suggested_action},
        };
    }

    return RunStatus{
        .profile_id = event.profile_id,
        .profile_name = context.profile_name,
        .run_id = event.run_id,
        .state = state,
        .phase = phase,
        .message = message_for_event(event),
        .current_source_name = source_name == context.source_names.end() ? source_id : source_name->second,
        .target_name = context.target_name,
        .source_index = event.source_index,
        .source_count = context.source_count,
        .started_at = context.started_at,
        .updated_at = updated_at,
        .finished_at = finished_at,
        .error = std::move(error),
        .details = details,
        .can_cancel = can_cancel,
        .progress = RunProgress{
            .processed_bytes = bytes_processed,
            .estimated_bytes = bytes_total_estimated == 0
                ? std::nullopt
                : std::optional<std::uint64_t>{bytes_total_estimated},
            .run_processed_bytes = run_bytes_processed,
            .speed_bps = speed_bps,
            .eta_seconds = eta_seconds < 0 ? std::nullopt : std::optional<int>{eta_seconds},
            .source_percent = source_progress < 0 ? std::nullopt : std::optional<int>{source_progress},
            .overall_percent = overall_progress < 0 ? std::nullopt : std::optional<int>{overall_progress},
            .accuracy = progress_accuracy == "estimated"
                ? ProgressAccuracy::Estimated
                : ProgressAccuracy::Indeterminate,
        },
        .exit_code = exit_code,
    };
}

bool should_write_status(btrfsbackup::backup::BackupRunEventKind kind) {
    return kind == btrfsbackup::backup::BackupRunEventKind::RunStarted || kind == btrfsbackup::backup::BackupRunEventKind::SourceStarted || kind == btrfsbackup::backup::BackupRunEventKind::ActionStarted || kind == btrfsbackup::backup::BackupRunEventKind::TransferProgress || kind == btrfsbackup::backup::BackupRunEventKind::ActionCompleted || kind == btrfsbackup::backup::BackupRunEventKind::ActionFailed || kind == btrfsbackup::backup::BackupRunEventKind::SourceCompleted || kind == btrfsbackup::backup::BackupRunEventKind::RunCompleted || kind == btrfsbackup::backup::BackupRunEventKind::RunFailed || kind == btrfsbackup::backup::BackupRunEventKind::RunCancelled;
}

bool should_write_history(btrfsbackup::backup::BackupRunEventKind kind) {
    return kind == btrfsbackup::backup::BackupRunEventKind::RunFailed || kind == btrfsbackup::backup::BackupRunEventKind::RunCompleted || kind == btrfsbackup::backup::BackupRunEventKind::RunCancelled;
}

} // namespace

RunStatusProjection::RunStatusProjection(IAtomicDocumentWriter& files, BackupRunStatusContext context)
    : files_(files), context_(std::move(context)) {
}

void RunStatusProjection::on_backup_run_event(const btrfsbackup::backup::BackupRunEvent& event) {
    EventProjectionData data = event_projection_data(event);
    if (!should_write_status(data.kind)) {
        return;
    }

    if (data.run_id != run_id_) {
        run_id_ = data.run_id;
        pending_action_failure_.reset();
        run_started_ = data.kind == btrfsbackup::backup::BackupRunEventKind::RunStarted;
        last_overall_progress_ = -1;
    } else if (data.kind == btrfsbackup::backup::BackupRunEventKind::RunStarted) {
        run_started_ = true;
        pending_action_failure_.reset();
        last_overall_progress_ = -1;
    }
    if (data.kind == btrfsbackup::backup::BackupRunEventKind::ActionFailed &&
        data.source_id.has_value() && data.action_kind.has_value()) {
        pending_action_failure_ = PendingActionFailure{
            .run_id = data.run_id,
            .source_id = *data.source_id,
            .source_index = data.source_index,
            .action_kind = *data.action_kind,
        };
    } else if (data.kind == btrfsbackup::backup::BackupRunEventKind::RunFailed && pending_action_failure_.has_value() && pending_action_failure_->run_id == data.run_id) {
        data.source_id = pending_action_failure_->source_id;
        data.source_index = pending_action_failure_->source_index;
        data.action_kind = pending_action_failure_->action_kind;
    }
    RunStatus status = status_for_event(context_, data, last_overall_progress_);
    if (status.progress.overall_percent.has_value()) {
        last_overall_progress_ = *status.progress.overall_percent;
    }
    if (data.kind != btrfsbackup::backup::BackupRunEventKind::RunFailed || run_started_) {
        write_current_status(files_, context_.status_root, status);
    }
    if (should_write_history(data.kind)) {
        write_history_entry(files_, context_.history_root, status);
        pending_action_failure_.reset();
        run_started_ = false;
    }
}

} // namespace btrfsbackup::state
