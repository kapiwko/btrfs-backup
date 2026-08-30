// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/StatusWriter.hpp>

#include <filesystem>
#include <string>
#include <variant>

#include <core/Errors.hpp>
#include <core/Identifiers.hpp>
#include <core/RuntimeTime.hpp>
#include <config/json/JsonIo.hpp>

namespace fs = std::filesystem;

namespace {

constexpr fs::perms public_status_file_permissions =
    fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read | fs::perms::others_read;
constexpr fs::perms public_status_directory_permissions =
    public_status_file_permissions | fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec;

void require_non_empty(const std::string& value, const char* field) {
    if (value.empty()) {
        throw btrfsbackup::ValidationError(std::string(field) + " is required");
    }
}

void validate_status(const btrfsbackup::state::RunStatus& status) {
    require_non_empty(status.profile_name, "profileName");
    btrfsbackup::state::validate_run_status(status);
}

void prepare_public_parent(btrfsbackup::state::IAtomicDocumentWriter& files, const fs::path& path) {
    files.ensure_directory(path.parent_path(), public_status_directory_permissions);
}

btrfsbackup::config::json::Json build_details_json(const btrfsbackup::state::RunDetails& details) {
    btrfsbackup::config::json::Json json = btrfsbackup::config::json::Json::object();
    for (const auto& [name, value] : details) {
        std::visit([&](const auto& item) { json[name] = item; }, value);
    }
    return json;
}

std::string public_activity(const btrfsbackup::state::RunStatus& status) {
    using btrfsbackup::state::RunPhase;
    using btrfsbackup::state::RunState;
    if (status.state != RunState::Running && status.state != RunState::Validating) {
        return "idle";
    }
    switch (status.phase) {
    case RunPhase::Sizing:
        return "sizing";
    case RunPhase::Transferring:
        return "transferring";
    case RunPhase::VerifyReceived:
    case RunPhase::CommitReceived:
    case RunPhase::ApplyRemoteRetention:
    case RunPhase::ApplyLocalRetention:
    case RunPhase::CleanupSource:
    case RunPhase::SourceCompleted:
        return "finalizing";
    default:
        return "preparing";
    }
}

} // namespace

namespace btrfsbackup::state {

btrfsbackup::config::json::Json build_status_json(const RunStatus& status) {
    validate_status(status);

    const RunError* error = status.error ? &*status.error : nullptr;

    return {
        {"schemaVersion", 2},
        {"profileId", std::string(status.profile_id.value())},
        {"profileName", status.profile_name},
        {"runId", std::string(status.run_id.value())},
        {"state", run_state_name(status.state)},
        {"phase", run_phase_name(status.phase)},
        {"message", status.message},
        {"currentSourceName", status.current_source_name},
        {"targetName", status.target_name},
        {"sourceIndex", status.source_index},
        {"sourceCount", status.source_count},
        {"startedAt", format_utc_iso_timestamp(status.started_at)},
        {"updatedAt", format_utc_iso_timestamp(status.updated_at)},
        {"finishedAt", status.finished_at.has_value() ? format_utc_iso_timestamp(*status.finished_at) : ""},
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
    return btrfsbackup::config::json::dump_json(build_status_json(status));
}

btrfsbackup::config::json::Json build_public_status_json(const RunStatus& status) {
    validate_status(status);
    const std::string public_error_code = !status.error.has_value()
        ? ""
        : (status.state == RunState::Cancelled ? "backup.cancelled" : "backup.failed");

    return {
        {"schemaVersion", 3},
        {"runId", std::string(status.run_id.value())},
        {"state", run_state_name(status.state)},
        {"phase", run_phase_name(status.phase)},
        {"activity", public_activity(status)},
        {"canCancel", status.can_cancel},
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
    return btrfsbackup::config::json::dump_json(build_public_status_json(status));
}

void write_current_status(
    IAtomicDocumentWriter& files,
    const fs::path& status_root,
    const RunStatus& status
) {
    std::string content = dump_public_status_json(status);
    fs::path path = status_root / status.profile_id.value() / "current.json";
    prepare_public_parent(files, path);
    files.write_atomically(path, content, public_status_file_permissions);
}

} // namespace btrfsbackup::state
