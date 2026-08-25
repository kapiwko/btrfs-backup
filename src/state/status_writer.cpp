// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/status_writer.hpp>

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <variant>

#include <config/errors.hpp>
#include <platform/linux/file_io.hpp>
#include <config/identifiers.hpp>
#include <config/json_io.hpp>

namespace fs = std::filesystem;

namespace {

void require_non_empty(const std::string& value, const char* field) {
    if (value.empty()) {
        throw btrfsbackup::ValidationError(std::string(field) + " is required");
    }
}

void validate_status(const btrfsbackup::RunStatus& status) {
    btrfsbackup::validate_profile_id(status.profile_id.value);
    require_non_empty(status.profile_name, "profileName");
    require_non_empty(status.run_id.value, "runId");
    btrfsbackup::validate_run_id(status.run_id.value);
    require_non_empty(status.started_at, "startedAt");
    require_non_empty(status.updated_at, "updatedAt");
}

void set_directory_mode(const fs::path& path, mode_t mode) {
    int result;
    do {
        result = chmod(path.c_str(), mode);
    } while (result != 0 && errno == EINTR);
    if (result != 0) {
        throw btrfsbackup::ValidationError(
            "cannot set permissions on " + path.string() + ": " + std::strerror(errno)
        );
    }
}

void prepare_public_parent(const fs::path& path) {
    fs::create_directories(path.parent_path());
    set_directory_mode(path.parent_path(), 0755);
}

void prepare_private_history_directory(const fs::path& history_root, const fs::path& directory) {
    fs::create_directories(history_root);
    set_directory_mode(history_root, 0700);
    fs::create_directories(directory);
    set_directory_mode(directory, 0700);
}

btrfsbackup::Json build_details_json(const btrfsbackup::RunDetails& details) {
    btrfsbackup::Json json = btrfsbackup::Json::object();
    for (const auto& [name, value] : details) {
        std::visit([&](const auto& item) { json[name] = item; }, value);
    }
    return json;
}

} // namespace

namespace btrfsbackup {

Json build_status_json(const RunStatus& status) {
    validate_status(status);

    const RunError* error = status.error ? &*status.error : nullptr;

    return {
        {"schemaVersion", 2},
        {"profileId", status.profile_id.value},
        {"profileName", status.profile_name},
        {"runId", status.run_id.value},
        {"state", run_state_name(status.state)},
        {"phase", run_phase_name(status.phase)},
        {"message", status.message},
        {"currentSourceName", status.current_source_name},
        {"targetName", status.target_name},
        {"sourceIndex", status.source_index},
        {"sourceCount", status.source_count},
        {"startedAt", status.started_at},
        {"updatedAt", status.updated_at},
        {"finishedAt", status.finished_at},
        {"errorCode", error == nullptr ? "" : error_code_name(error->code)},
        {"errorMessage", error == nullptr ? "" : error->message},
        {"details", build_details_json(status.details)},
        {"recoverable", error != nullptr && error->recoverable},
        {"suggestedAction", error == nullptr ? "" : error->suggested_action.value},
        {"canCancel", status.can_cancel},
        {"bytesProcessed", status.progress.processed_bytes},
        {"bytesTotalEstimated", status.progress.estimated_bytes.value_or(0)},
        {"runBytesProcessed", status.progress.run_processed_bytes},
        {"speedBps", status.progress.speed_bps},
        {"etaSeconds", status.progress.eta_seconds.value_or(-1)},
        {"sourceProgress", status.progress.source_percent.value_or(-1)},
        {"overallProgress", status.progress.overall_percent.value_or(-1)},
        {"progressAccuracy", progress_accuracy_name(status.progress.accuracy)},
        {"exitCode", status.exit_code},
    };
}

std::string dump_status_json(const RunStatus& status) {
    return dump_json(build_status_json(status));
}

Json build_public_status_json(const RunStatus& status) {
    validate_status(status);
    const std::string public_error_code = !status.error.has_value()
        ? ""
        : (status.state == RunState::Cancelled ? "backup.cancelled" : "backup.failed");

    return {
        {"schemaVersion", 3},
        {"state", run_state_name(status.state)},
        {"errorCode", public_error_code},
        {"sourceName", status.current_source_name},
        {"targetName", status.target_name},
        {"speedBps", status.progress.speed_bps},
        {"etaSeconds", status.progress.eta_seconds.value_or(-1)},
        {"sourceProgress", status.progress.source_percent.value_or(-1)},
        {"overallProgress", status.progress.overall_percent.value_or(-1)},
        {"progressAccuracy", progress_accuracy_name(status.progress.accuracy)},
    };
}

std::string dump_public_status_json(const RunStatus& status) {
    return dump_json(build_public_status_json(status));
}

void write_current_status(const fs::path& status_root, const RunStatus& status, mode_t mode) {
    std::string content = dump_public_status_json(status);
    fs::path path = status_root / status.profile_id.value / "current.json";
    prepare_public_parent(path);
    atomic_write(path, content, mode);
}

void write_history_entry(const fs::path& history_root, const RunStatus& status) {
    std::string content = dump_status_json(status);
    fs::path directory = history_root / status.profile_id.value;
    fs::path run_path = directory / (status.run_id.value + ".json");
    fs::path last_path = directory / "last.json";

    prepare_private_history_directory(history_root, directory);
    atomic_write(run_path, content, 0600);
    atomic_write(last_path, content, 0600);
}

} // namespace btrfsbackup
