#include <btrfsbackup/status_writer.hpp>

#include <filesystem>
#include <string>
#include <sys/stat.h>

#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/file_io.hpp>
#include <btrfsbackup/identifiers.hpp>
#include <btrfsbackup/json_io.hpp>

namespace fs = std::filesystem;

namespace {

void require_non_empty(const std::string& value, const char* field) {
    if (value.empty()) {
        throw btrfsbackup::ValidationError(std::string(field) + " is required");
    }
}

void prepare_public_parent(const fs::path& path) {
    fs::create_directories(path.parent_path());
    chmod(path.parent_path().c_str(), 0755);
}

} // namespace

namespace btrfsbackup {

Json build_status_json(const RunStatusRecord& record) {
    validate_profile_id(record.profile_id);
    require_non_empty(record.profile_name, "profileName");
    require_non_empty(record.run_id, "runId");
    validate_run_id(record.run_id);
    require_non_empty(record.state, "state");
    require_non_empty(record.phase, "phase");
    require_non_empty(record.started_at, "startedAt");
    require_non_empty(record.updated_at, "updatedAt");

    return {
        {"schemaVersion", 2},
        {"profileId", record.profile_id},
        {"profileName", record.profile_name},
        {"runId", record.run_id},
        {"state", record.state},
        {"phase", record.phase},
        {"message", record.message},
        {"currentSourceName", record.current_source_name},
        {"sourceIndex", record.source_index},
        {"sourceCount", record.source_count},
        {"startedAt", record.started_at},
        {"updatedAt", record.updated_at},
        {"finishedAt", record.finished_at},
        {"errorCode", record.error_code},
        {"errorMessage", record.error_message},
        {"details", record.details.is_null() ? Json::object() : record.details},
        {"recoverable", record.recoverable},
        {"suggestedAction", record.suggested_action},
        {"canCancel", record.can_cancel},
        {"bytesProcessed", record.bytes_processed},
        {"bytesTotalEstimated", record.bytes_total_estimated},
        {"runBytesProcessed", record.run_bytes_processed},
        {"speedBps", record.speed_bps},
        {"etaSeconds", record.eta_seconds},
        {"sourceProgress", record.source_progress},
        {"overallProgress", record.overall_progress},
        {"progressAccuracy", record.progress_accuracy},
        {"exitCode", record.exit_code},
    };
}

std::string dump_status_json(const RunStatusRecord& record) {
    return dump_json(build_status_json(record));
}

void write_current_status(const fs::path& status_root, const RunStatusRecord& record, mode_t mode) {
    std::string content = dump_status_json(record);
    fs::path path = status_root / record.profile_id / "current.json";
    prepare_public_parent(path);
    atomic_write(path, content, mode);
}

void write_history_entry(const fs::path& history_root, const RunStatusRecord& record, mode_t mode) {
    std::string content = dump_status_json(record);
    fs::path directory = history_root / record.profile_id;
    fs::path run_path = directory / (record.run_id + ".json");
    fs::path last_path = directory / "last.json";

    prepare_public_parent(run_path);
    atomic_write(run_path, content, mode);
    atomic_write(last_path, content, mode);
}

} // namespace btrfsbackup
