// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/StatusQueryService.hpp>

#include <string>
#include <utility>

#include <core/Errors.hpp>
#include <core/Identifiers.hpp>
#include <daemon/HistoryQueryService.hpp>
#include <daemon/ManagerDocumentReader.hpp>

namespace fs = std::filesystem;

namespace {

btrfsbackup::daemon::PublicRunStatus unavailable_status() {
    return {};
}

btrfsbackup::daemon::PublicRunStatus sanitize_public_status(const btrfsbackup::config::Json& input) {
    if (!input.is_object() || input.value("schemaVersion", 0) != 3) {
        throw btrfsbackup::ValidationError("public status has an unsupported schema");
    }
    for (const char* field : {
             "state",
             "errorCode",
             "sourceName",
             "targetName",
             "speedBps",
             "etaSeconds",
             "sourceProgress",
             "overallProgress",
             "progressAccuracy",
         }) {
        if (!input.contains(field)) {
            throw btrfsbackup::ValidationError(std::string("public status is missing field: ") + field);
        }
    }
    return {
        .run_id = input.value("runId", std::string{}),
        .state = input.at("state").get<std::string>(),
        .phase = input.value("phase", std::string{"idle"}),
        .activity = input.value("activity", std::string{"idle"}),
        .can_cancel = input.value("canCancel", false),
        .error_code = input.at("errorCode").get<std::string>(),
        .source_name = input.at("sourceName").get<std::string>(),
        .target_name = input.at("targetName").get<std::string>(),
        .speed_bps = input.at("speedBps").get<std::int64_t>(),
        .eta_seconds = input.at("etaSeconds").get<std::int64_t>(),
        .source_progress = input.at("sourceProgress").get<int>(),
        .overall_progress = input.at("overallProgress").get<int>(),
        .progress_accuracy = input.at("progressAccuracy").get<std::string>(),
    };
}

} // namespace

namespace btrfsbackup::daemon {

StatusQueryService::StatusQueryService(
    fs::path status_root,
    const HistoryQueryService& history
)
    : status_root_(std::move(status_root)), history_(history) {
}

PublicRunStatus StatusQueryService::get_status(const std::string& profile_id) const {
    validate_profile_id(profile_id);
    const fs::path current = status_root_ / profile_id / "current.json";
    if (manager_regular_file_if_present(current)) {
        return sanitize_public_status(read_manager_json_document(current));
    }

    const std::optional<SanitizedHistoryEntry> last = history_.get_last_sanitized(profile_id);
    if (last.has_value()) {
        PublicRunStatus result;
        result.state = last->state;
        result.phase = "idle";
        result.activity = "idle";
        result.error_code = last->error_code;
        result.source_name = last->source_name;
        result.target_name = last->target_name;
        result.overall_progress = last->overall_progress;
        return result;
    }
    return unavailable_status();
}

} // namespace btrfsbackup::daemon
