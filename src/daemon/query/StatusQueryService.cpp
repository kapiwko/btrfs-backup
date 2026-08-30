// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/query/StatusQueryService.hpp>

#include <string>
#include <utility>

#include <core/Errors.hpp>
#include <core/Identifiers.hpp>
#include <daemon/query/HistoryQueryService.hpp>
#include <daemon/query/ManagerDocumentReader.hpp>
#include <state/document/RunStatusDocumentCodec.hpp>
#include <state/query/RunState.hpp>

namespace fs = std::filesystem;

namespace {

void set_history_state(btrfsbackup::daemon::PublicRunStatus& result, const std::string& state) {
    using btrfsbackup::state::document::PublicRunState;
    if (state == "succeeded")
        result.state = PublicRunState::Succeeded;
    else if (state == "failed")
        result.state = PublicRunState::Failed;
    else if (state == "cancelled")
        result.state = PublicRunState::Cancelled;
    else if (state == "skipped")
        result.state = PublicRunState::Skipped;
    else if (state == "validated")
        result.state = PublicRunState::Validated;
    else {
        result.state = PublicRunState::Unknown;
        result.unknown_state = state;
    }
}

} // namespace

namespace btrfsbackup::daemon::query {

StatusQueryService::StatusQueryService(
    fs::path status_root,
    fs::path state_root,
    const HistoryQueryService& history
)
    : status_root_(std::move(status_root)),
      state_root_(std::move(state_root)),
      history_(history) {
}

PublicStatusResponse StatusQueryService::get_status(const std::string& profile_id) const {
    validate_profile_id(profile_id);
    PublicStatusResponse response;
    const std::optional<SanitizedHistoryEntry> last = history_.get_last_sanitized(profile_id);
    if (last.has_value()) {
        response.last_attempt_at = last->finished_at;
        response.last_attempt_state = last->state;
    }
    const fs::path last_success = state_root_ / "profiles" / profile_id / "last-success";
    if (manager_regular_file_if_present(last_success)) {
        response.last_success_at = btrfsbackup::state::parse_last_success_timestamp(
                                       read_manager_document(last_success)
        )
                                       .value_or("");
    }

    const fs::path current = status_root_ / profile_id / "current.json";
    if (manager_regular_file_if_present(current)) {
        const btrfsbackup::state::document::RunStatusDocumentCodec codec;
        const std::string content = read_manager_document(current);
        response.run = codec.parse_public(content);
        const btrfsbackup::config::json::Json document = btrfsbackup::config::json::Json::parse(content);
        response.source_index = document.value("sourceIndex", 0);
        response.source_count = document.value("sourceCount", 0);
        response.started_at = document.value("startedAt", std::string{});
        response.updated_at = document.value("updatedAt", std::string{});
        return response;
    }

    if (last.has_value()) {
        set_history_state(response.run, last->state);
        response.run.phase = {.value = "idle", .known = true};
        response.run.activity = btrfsbackup::state::document::PublicActivity::Idle;
        response.run.error_code = last->error_code == "backup.cancelled"
            ? btrfsbackup::state::document::PublicErrorCode::Cancelled
            : (last->error_code.empty() ? btrfsbackup::state::document::PublicErrorCode::None
                                        : btrfsbackup::state::document::PublicErrorCode::Failed);
        response.run.source_name = last->source_name;
        response.run.target_name = last->target_name;
        response.source_index = last->source_count;
        response.source_count = last->source_count;
        response.started_at = last->started_at;
        response.updated_at = last->finished_at;
        if (last->overall_progress >= 0)
            response.run.progress.overall_percent = last->overall_progress;
    }
    return response;
}

} // namespace btrfsbackup::daemon::query
