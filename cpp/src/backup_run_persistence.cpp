#include <btrfsbackup/backup_run_persistence.hpp>

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <string>
#include <sys/stat.h>

#include <btrfsbackup/file_io.hpp>
#include <btrfsbackup/identifiers.hpp>
#include <btrfsbackup/json_io.hpp>

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

std::string phase_for_action(BackupRunActionKind kind) {
    return backup_run_action_kind_name(kind);
}

std::string error_code_for_failed_action(BackupRunActionKind kind) {
    switch (kind) {
        case BackupRunActionKind::BeforeSnapshotHook:
            return "hook.before_snapshot_failed";
        case BackupRunActionKind::AfterSnapshotHook:
            return "hook.after_snapshot_failed";
        default:
            return "runner.action_failed";
    }
}

bool failed_action_is_recoverable(BackupRunActionKind kind) {
    return kind == BackupRunActionKind::BeforeSnapshotHook
        || kind == BackupRunActionKind::AfterSnapshotHook;
}

std::string suggested_action_for_failed_action(BackupRunActionKind kind) {
    if (kind == BackupRunActionKind::BeforeSnapshotHook
        || kind == BackupRunActionKind::AfterSnapshotHook) {
        return "inspect-hook-program";
    }
    return "inspect-run-history";
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

StatusRecord status_record_for_event(const BackupRunStatusContext& context, const BackupRunEvent& event) {
    std::string state = "running";
    std::string phase = backup_run_event_kind_name(event.kind);
    std::string finished_at;
    int exit_code = 0;

    if (event.kind == BackupRunEventKind::ActionStarted
        || event.kind == BackupRunEventKind::ActionCompleted
        || event.kind == BackupRunEventKind::ActionFailed
        || event.kind == BackupRunEventKind::CheckpointWritten) {
        phase = phase_for_action(event.action_kind);
    } else if (event.kind == BackupRunEventKind::TransferProgress) {
        phase = "transferring";
    }

    if (event.kind == BackupRunEventKind::ActionFailed) {
        state = "failed";
        exit_code = 1;
        finished_at = current_utc_iso_timestamp();
    } else if (event.kind == BackupRunEventKind::RunCompleted) {
        state = "succeeded";
        phase = "succeeded";
        finished_at = current_utc_iso_timestamp();
    } else if (event.kind == BackupRunEventKind::RunCancelled) {
        state = "cancelled";
        phase = "cancelled";
        exit_code = 130;
        finished_at = current_utc_iso_timestamp();
    }

    Json details = Json::object();
    std::string error_code;
    std::string error_message;
    bool recoverable = false;
    std::string suggested_action;
    Json progress_details = Json::object();
    bool can_cancel = false;
    std::uint64_t bytes_processed = 0;
    std::uint64_t speed_bps = 0;
    if (event.kind == BackupRunEventKind::TransferProgress) {
        can_cancel = true;
        bytes_processed = event.bytes_transferred;
        speed_bps = event.speed_bps;
        progress_details = {
            {"bytesProduced", event.bytes_produced},
            {"bytesTransferred", event.bytes_transferred},
            {"deltaBytes", event.delta_bytes},
            {"pendingBytes", event.pending_bytes},
            {"elapsedMs", event.elapsed_ms},
            {"speedBps", event.speed_bps}
        };
        details = progress_details;
    }
    if (event.kind == BackupRunEventKind::ActionFailed) {
        error_code = event.error_code.empty() ? error_code_for_failed_action(event.action_kind) : event.error_code;
        error_message = event.message;
        details = {
            {"sourceId", event.source_id},
            {"action", backup_run_action_kind_name(event.action_kind)}
        };
        recoverable = failed_action_is_recoverable(event.action_kind);
        suggested_action = suggested_action_for_failed_action(event.action_kind);
    } else if (event.kind == BackupRunEventKind::RunCancelled) {
        error_code = "runner.cancelled";
        error_message = message_for_event(event);
        details = {
            {"sourceId", event.source_id},
            {"action", backup_run_action_kind_name(event.action_kind)}
        };
        recoverable = true;
        suggested_action = "run-backup-again";
    }

    return StatusRecord{
        .profile_id = event.profile_id,
        .profile_name = context.profile_name,
        .run_id = event.run_id,
        .state = state,
        .phase = phase,
        .message = message_for_event(event),
        .current_source_name = event.source_id,
        .source_index = 0,
        .source_count = context.source_count,
        .started_at = context.started_at,
        .updated_at = current_utc_iso_timestamp(),
        .finished_at = finished_at,
        .error_code = error_code,
        .error_message = error_message,
        .details = details,
        .recoverable = recoverable,
        .suggested_action = suggested_action,
        .can_cancel = can_cancel,
        .bytes_processed = bytes_processed,
        .run_bytes_processed = bytes_processed,
        .speed_bps = speed_bps,
        .progress_accuracy = "indeterminate",
        .exit_code = exit_code,
    };
}

bool should_write_status(BackupRunEventKind kind) {
    return kind == BackupRunEventKind::RunStarted
        || kind == BackupRunEventKind::SourceStarted
        || kind == BackupRunEventKind::ActionStarted
        || kind == BackupRunEventKind::TransferProgress
        || kind == BackupRunEventKind::ActionCompleted
        || kind == BackupRunEventKind::ActionFailed
        || kind == BackupRunEventKind::SourceCompleted
        || kind == BackupRunEventKind::RunCompleted
        || kind == BackupRunEventKind::RunCancelled;
}

bool should_write_history(BackupRunEventKind kind) {
    return kind == BackupRunEventKind::ActionFailed
        || kind == BackupRunEventKind::RunCompleted
        || kind == BackupRunEventKind::RunCancelled;
}

} // namespace

std::string backup_run_action_kind_name(BackupRunActionKind kind) {
    switch (kind) {
        case BackupRunActionKind::RecoverPending:
            return "recover-pending";
        case BackupRunActionKind::CleanupIncoming:
            return "cleanup-incoming";
        case BackupRunActionKind::BeforeSnapshotHook:
            return "before-snapshot-hook";
        case BackupRunActionKind::CreateSnapshot:
            return "create-snapshot";
        case BackupRunActionKind::AfterSnapshotHook:
            return "after-snapshot-hook";
        case BackupRunActionKind::SelectParent:
            return "select-parent";
        case BackupRunActionKind::SendReceive:
            return "send-receive";
        case BackupRunActionKind::VerifyReceived:
            return "verify-received";
        case BackupRunActionKind::CommitReceived:
            return "commit-received";
        case BackupRunActionKind::ApplyRemoteRetention:
            return "apply-remote-retention";
        case BackupRunActionKind::ApplyLocalRetention:
            return "apply-local-retention";
        case BackupRunActionKind::CleanupSource:
            return "cleanup-source";
    }
    return "unknown";
}

std::string backup_run_event_kind_name(BackupRunEventKind kind) {
    switch (kind) {
        case BackupRunEventKind::RunStarted:
            return "run-started";
        case BackupRunEventKind::SourceStarted:
            return "source-started";
        case BackupRunEventKind::ActionStarted:
            return "action-started";
        case BackupRunEventKind::TransferProgress:
            return "transfer-progress";
        case BackupRunEventKind::ActionCompleted:
            return "action-completed";
        case BackupRunEventKind::ActionFailed:
            return "action-failed";
        case BackupRunEventKind::CheckpointWritten:
            return "checkpoint-written";
        case BackupRunEventKind::SourceCompleted:
            return "source-completed";
        case BackupRunEventKind::RunCompleted:
            return "run-completed";
        case BackupRunEventKind::RunCancelled:
            return "run-cancelled";
    }
    return "unknown";
}

Json build_backup_run_checkpoint_json(const BackupRunCheckpoint& checkpoint) {
    validate_profile_id(checkpoint.profile_id);
    validate_run_id(checkpoint.run_id);
    validate_identifier(checkpoint.source_id, "sourceId");
    return {
        {"schemaVersion", 1},
        {"profileId", checkpoint.profile_id},
        {"runId", checkpoint.run_id},
        {"sourceId", checkpoint.source_id},
        {"action", backup_run_action_kind_name(checkpoint.action_kind)},
        {"updatedAt", current_utc_iso_timestamp()},
    };
}

Json build_backup_run_event_json(const BackupRunEvent& event) {
    validate_profile_id(event.profile_id);
    validate_run_id(event.run_id);
    if (!event.source_id.empty()) {
        validate_identifier(event.source_id, "sourceId");
    }
    return {
        {"schemaVersion", 1},
        {"event", backup_run_event_kind_name(event.kind)},
        {"profileId", event.profile_id},
        {"runId", event.run_id},
        {"sourceId", event.source_id},
        {"action", backup_run_action_kind_name(event.action_kind)},
        {"bytesTransferred", event.bytes_transferred},
        {"bytesProduced", event.bytes_produced},
        {"deltaBytes", event.delta_bytes},
        {"pendingBytes", event.pending_bytes},
        {"elapsedMs", event.elapsed_ms},
        {"speedBps", event.speed_bps},
        {"errorCode", event.error_code},
        {"message", event.message},
    };
}

JsonFileBackupRunCheckpointStore::JsonFileBackupRunCheckpointStore(fs::path profile_state_dir)
    : profile_state_dir_(std::move(profile_state_dir)) {
}

void JsonFileBackupRunCheckpointStore::write_checkpoint(const BackupRunCheckpoint& checkpoint) {
    fs::create_directories(profile_state_dir_);
    chmod(profile_state_dir_.c_str(), 0700);
    atomic_write(profile_state_dir_ / "checkpoint.json", dump_json(build_backup_run_checkpoint_json(checkpoint)), 0600);
    fsync_dir(profile_state_dir_);
}

StatusBackupRunEventSink::StatusBackupRunEventSink(BackupRunStatusContext context)
    : context_(std::move(context)) {
}

void StatusBackupRunEventSink::on_backup_run_event(const BackupRunEvent& event) {
    if (!should_write_status(event.kind)) {
        return;
    }

    StatusRecord record = status_record_for_event(context_, event);
    write_current_status(context_.status_root, record);
    if (should_write_history(event.kind)) {
        write_history_entry(context_.history_root, record);
    }
}

} // namespace btrfsbackup
