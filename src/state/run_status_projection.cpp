// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/run_status_projection.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <state/backup_run_serialization.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup {

namespace {

std::string current_utc_iso_timestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&time, &tm);

    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

RunPhase phase_for_action(BackupRunActionKind kind) {
    switch (kind) {
    case BackupRunActionKind::RecoverPending:
        return RunPhase::RecoverPending;
    case BackupRunActionKind::CleanupIncoming:
        return RunPhase::CleanupIncoming;
    case BackupRunActionKind::BeforeSnapshotHook:
        return RunPhase::BeforeSnapshotHook;
    case BackupRunActionKind::CreateSnapshot:
        return RunPhase::CreateSnapshot;
    case BackupRunActionKind::AfterSnapshotHook:
        return RunPhase::AfterSnapshotHook;
    case BackupRunActionKind::SelectParent:
        return RunPhase::SelectParent;
    case BackupRunActionKind::SendReceive:
        return RunPhase::SendReceive;
    case BackupRunActionKind::VerifyReceived:
        return RunPhase::VerifyReceived;
    case BackupRunActionKind::CommitReceived:
        return RunPhase::CommitReceived;
    case BackupRunActionKind::ApplyRemoteRetention:
        return RunPhase::ApplyRemoteRetention;
    case BackupRunActionKind::ApplyLocalRetention:
        return RunPhase::ApplyLocalRetention;
    case BackupRunActionKind::CleanupSource:
        return RunPhase::CleanupSource;
    }
    return RunPhase::RunStarted;
}

RunPhase phase_for_event(BackupRunEventKind kind) {
    switch (kind) {
    case BackupRunEventKind::RunStarted:
        return RunPhase::RunStarted;
    case BackupRunEventKind::SourceStarted:
        return RunPhase::SourceStarted;
    case BackupRunEventKind::TransferProgress:
        return RunPhase::Transferring;
    case BackupRunEventKind::SourceCompleted:
        return RunPhase::SourceCompleted;
    case BackupRunEventKind::RunCompleted:
        return RunPhase::Succeeded;
    case BackupRunEventKind::RunCancelled:
        return RunPhase::Cancelled;
    case BackupRunEventKind::ActionStarted:
    case BackupRunEventKind::ActionCompleted:
    case BackupRunEventKind::ActionFailed:
    case BackupRunEventKind::CheckpointWritten:
        return RunPhase::RunStarted;
    }
    return RunPhase::RunStarted;
}

ErrorCode error_code_for_failed_action(BackupRunActionKind kind) {
    switch (kind) {
    case BackupRunActionKind::BeforeSnapshotHook:
        return ErrorCode::HookBeforeSnapshotFailed;
    case BackupRunActionKind::AfterSnapshotHook:
        return ErrorCode::HookAfterSnapshotFailed;
    default:
        return ErrorCode::RunnerActionFailed;
    }
}

bool failed_action_is_recoverable(BackupRunActionKind kind) {
    return kind == BackupRunActionKind::BeforeSnapshotHook || kind == BackupRunActionKind::AfterSnapshotHook;
}

std::string suggested_action_for_failed_action(BackupRunActionKind kind) {
    if (kind == BackupRunActionKind::BeforeSnapshotHook || kind == BackupRunActionKind::AfterSnapshotHook) {
        return "inspect-hook-program";
    }
    return "inspect-run-history";
}

int estimated_overall_progress(
    const BackupRunStatusContext& context,
    const BackupRunEvent& event,
    int source_progress
) {
    if (context.source_count <= 0) {
        return -1;
    }
    if (event.kind == BackupRunEventKind::RunCompleted) {
        return 100;
    }
    if (event.kind == BackupRunEventKind::SourceCompleted && event.source_index > 0) {
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

int estimated_source_progress(const BackupRunEvent& event) {
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

int estimated_eta_seconds(const BackupRunEvent& event) {
    if (event.bytes_total_estimated == 0 || event.speed_bps == 0 || event.bytes_transferred >= event.bytes_total_estimated) {
        return -1;
    }
    std::uint64_t remaining = event.bytes_total_estimated - event.bytes_transferred;
    const std::uint64_t seconds = remaining / event.speed_bps + (remaining % event.speed_bps == 0 ? 0 : 1);
    return seconds > static_cast<std::uint64_t>(std::numeric_limits<int>::max())
        ? std::numeric_limits<int>::max()
        : static_cast<int>(seconds);
}

std::string message_for_event(const BackupRunEvent& event) {
    if (!event.message.empty()) {
        return event.message;
    }
    switch (event.kind) {
    case BackupRunEventKind::RunStarted:
        return "Backup run started.";
    case BackupRunEventKind::SourceStarted:
        return "Backup source started.";
    case BackupRunEventKind::ActionStarted:
        return "Backup action started: " + backup_run_action_kind_name(event.action_kind) + ".";
    case BackupRunEventKind::TransferProgress:
        return "Backup transfer is running.";
    case BackupRunEventKind::ActionCompleted:
        return "Backup action completed: " + backup_run_action_kind_name(event.action_kind) + ".";
    case BackupRunEventKind::ActionFailed:
        return "Backup action failed: " + backup_run_action_kind_name(event.action_kind) + ".";
    case BackupRunEventKind::CheckpointWritten:
        return "Backup checkpoint written.";
    case BackupRunEventKind::SourceCompleted:
        return "Backup source completed.";
    case BackupRunEventKind::RunCompleted:
        return "Backup completed.";
    case BackupRunEventKind::RunCancelled:
        return "Backup cancelled.";
    }
    return "Backup event.";
}

RunStatus status_for_event(
    const BackupRunStatusContext& context,
    const BackupRunEvent& event,
    int minimum_overall_progress
) {
    const std::string source_id = event.source_id.has_value()
        ? std::string(event.source_id->value())
        : std::string{};
    auto source_name = context.source_names.find(source_id);
    RunState state = RunState::Running;
    RunPhase phase = phase_for_event(event.kind);
    std::string finished_at;
    int exit_code = 0;

    if (event.kind == BackupRunEventKind::ActionStarted || event.kind == BackupRunEventKind::ActionCompleted || event.kind == BackupRunEventKind::ActionFailed || event.kind == BackupRunEventKind::CheckpointWritten) {
        phase = phase_for_action(event.action_kind);
    } else if (event.kind == BackupRunEventKind::TransferProgress) {
        phase = RunPhase::Transferring;
    }

    if (event.kind == BackupRunEventKind::ActionFailed) {
        state = RunState::Failed;
        exit_code = 1;
        finished_at = current_utc_iso_timestamp();
    } else if (event.kind == BackupRunEventKind::RunCompleted) {
        state = RunState::Succeeded;
        phase = RunPhase::Succeeded;
        finished_at = current_utc_iso_timestamp();
    } else if (event.kind == BackupRunEventKind::RunCancelled) {
        state = RunState::Cancelled;
        phase = RunPhase::Cancelled;
        exit_code = 130;
        finished_at = current_utc_iso_timestamp();
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
    int source_progress = event.kind == BackupRunEventKind::TransferProgress
        ? estimated_source_progress(event)
        : -1;
    int overall_progress = estimated_overall_progress(context, event, source_progress);
    if (minimum_overall_progress >= 0 && event.kind != BackupRunEventKind::RunStarted) {
        overall_progress = std::max(overall_progress, minimum_overall_progress);
    }
    std::string progress_accuracy = overall_progress >= 0 ? "estimated" : "indeterminate";
    if (event.kind == BackupRunEventKind::TransferProgress) {
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
    if (event.kind == BackupRunEventKind::ActionFailed) {
        error_code = event.error_code.value_or(error_code_for_failed_action(event.action_kind));
        error_message = event.message;
        details = {
            {"sourceId", source_id},
            {"action", backup_run_action_kind_name(event.action_kind)}
        };
        if (error_code == ErrorCode::RepositoryRecoveryRequired) {
            recoverable = true;
            suggested_action = "run-backup-recovery";
        } else {
            recoverable = failed_action_is_recoverable(event.action_kind);
            suggested_action = suggested_action_for_failed_action(event.action_kind);
        }
    } else if (event.kind == BackupRunEventKind::RunCancelled) {
        error_code = ErrorCode::RunnerCancelled;
        error_message = message_for_event(event);
        details = {
            {"sourceId", source_id},
            {"action", backup_run_action_kind_name(event.action_kind)}
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
        .updated_at = current_utc_iso_timestamp(),
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

bool should_write_status(BackupRunEventKind kind) {
    return kind == BackupRunEventKind::RunStarted || kind == BackupRunEventKind::SourceStarted || kind == BackupRunEventKind::ActionStarted || kind == BackupRunEventKind::TransferProgress || kind == BackupRunEventKind::ActionCompleted || kind == BackupRunEventKind::ActionFailed || kind == BackupRunEventKind::SourceCompleted || kind == BackupRunEventKind::RunCompleted || kind == BackupRunEventKind::RunCancelled;
}

bool should_write_history(BackupRunEventKind kind) {
    return kind == BackupRunEventKind::ActionFailed || kind == BackupRunEventKind::RunCompleted || kind == BackupRunEventKind::RunCancelled;
}

} // namespace

RunStatusProjection::RunStatusProjection(BackupRunStatusContext context)
    : context_(std::move(context)) {
}

void RunStatusProjection::on_backup_run_event(const BackupRunEvent& event) {
    if (!should_write_status(event.kind)) {
        return;
    }

    if (event.kind == BackupRunEventKind::RunStarted || event.run_id != run_id_) {
        run_id_ = event.run_id;
        last_overall_progress_ = -1;
    }
    RunStatus status = status_for_event(context_, event, last_overall_progress_);
    if (status.progress.overall_percent.has_value()) {
        last_overall_progress_ = *status.progress.overall_percent;
    }
    write_current_status(context_.status_root, status);
    if (should_write_history(event.kind)) {
        write_history_entry(context_.history_root, status);
    }
}

} // namespace btrfsbackup
