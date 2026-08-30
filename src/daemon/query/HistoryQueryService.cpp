// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/query/HistoryQueryService.hpp>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <core/Errors.hpp>
#include <core/Identifiers.hpp>
#include <daemon/query/ManagerDocumentReader.hpp>
#include <state/document/LatestRunHistoryDocumentReader.hpp>
#include <state/document/RunStatusDocumentCodec.hpp>

namespace fs = std::filesystem;

namespace {

constexpr std::size_t max_history_limit = 100;
constexpr std::size_t max_history_offset = 10000;

using PrivateHistory = btrfsbackup::state::document::PrivateRunHistoryV2;

btrfsbackup::daemon::SanitizedHistoryEntry sanitize_private_history(const PrivateHistory& input) {
    const std::string& state = input.state.value;
    const std::string& detailed_error = input.error_code;
    return {
        .state = state,
        .error_code = detailed_error.empty() ? "" : (state == "cancelled" ? "backup.cancelled" : "backup.failed"),
        .source_name = input.current_source_name,
        .target_name = input.target_name,
        .started_at = input.started_at,
        .finished_at = input.finished_at,
        .source_count = input.source_count,
        .overall_progress = input.progress.overall_percent.value_or(-1),
    };
}

std::vector<fs::path> history_paths(const fs::path& root, const std::string& profile_id) {
    const fs::path directory = root / profile_id;
    std::error_code error;
    if (!fs::is_directory(directory, error) || error) {
        return {};
    }

    std::vector<fs::path> result;
    for (const auto& entry : fs::directory_iterator(directory, error)) {
        if (error) {
            break;
        }
        const std::string name = entry.path().filename().string();
        if (name != "last.json" && entry.path().extension() == ".json" && btrfsbackup::daemon::query::manager_regular_file_without_symlink(entry)) {
            result.push_back(entry.path());
        }
    }
    if (error) {
        throw btrfsbackup::ValidationError("cannot enumerate history for profile " + profile_id);
    }
    std::sort(result.rbegin(), result.rend());
    return result;
}

} // namespace

namespace btrfsbackup::daemon::query {

HistoryQueryService::HistoryQueryService(fs::path history_root)
    : history_root_(std::move(history_root)) {
}

SanitizedHistoryPage HistoryQueryService::get_history_sanitized(
    const std::string& profile_id,
    std::size_t offset,
    std::size_t limit
) const {
    validate_profile_id(profile_id);
    if (limit == 0 || limit > max_history_limit) {
        throw ValidationError("history limit must be between 1 and 100");
    }
    if (offset > max_history_offset) {
        throw ValidationError("history offset exceeds 10000");
    }

    const std::vector<fs::path> files = history_paths(history_root_, profile_id);
    const btrfsbackup::state::document::RunStatusDocumentCodec codec;
    SanitizedHistoryPage result;
    if (offset >= files.size()) {
        return result;
    }
    const std::size_t end = std::min(files.size(), offset + limit);
    for (std::size_t index = offset; index < end; ++index) {
        result.entries.push_back(sanitize_private_history(codec.parse_private(read_manager_document(files[index]))));
    }
    return result;
}

std::optional<SanitizedHistoryEntry> HistoryQueryService::get_last_sanitized(
    const std::string& profile_id
) const {
    validate_profile_id(profile_id);
    const btrfsbackup::state::document::LatestRunHistoryDocumentReader reader;
    std::optional<btrfsbackup::state::document::PrivateRunHistoryDocument> latest =
        reader.read(history_root_ / profile_id);
    if (!latest.has_value()) {
        return std::nullopt;
    }
    return sanitize_private_history(latest->history);
}

} // namespace btrfsbackup::daemon::query
