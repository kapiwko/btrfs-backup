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

namespace fs = std::filesystem;

namespace {

btrfsbackup::daemon::PublicRunStatus unavailable_status() {
    return {};
}

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
    const HistoryQueryService& history
)
    : status_root_(std::move(status_root)), history_(history) {
}

PublicRunStatus StatusQueryService::get_status(const std::string& profile_id) const {
    validate_profile_id(profile_id);
    const fs::path current = status_root_ / profile_id / "current.json";
    if (manager_regular_file_if_present(current)) {
        const btrfsbackup::state::document::RunStatusDocumentCodec codec;
        return codec.parse_public(read_manager_document(current));
    }

    const std::optional<SanitizedHistoryEntry> last = history_.get_last_sanitized(profile_id);
    if (last.has_value()) {
        PublicRunStatus result;
        set_history_state(result, last->state);
        result.phase = {.value = "idle", .known = true};
        result.activity = btrfsbackup::state::document::PublicActivity::Idle;
        result.error_code = last->error_code == "backup.cancelled"
            ? btrfsbackup::state::document::PublicErrorCode::Cancelled
            : (last->error_code.empty() ? btrfsbackup::state::document::PublicErrorCode::None
                                        : btrfsbackup::state::document::PublicErrorCode::Failed);
        result.source_name = last->source_name;
        result.target_name = last->target_name;
        if (last->overall_progress >= 0)
            result.progress.overall_percent = last->overall_progress;
        return result;
    }
    return unavailable_status();
}

} // namespace btrfsbackup::daemon::query
