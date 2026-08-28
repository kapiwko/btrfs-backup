// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/history_query_service.hpp>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <core/errors.hpp>
#include <core/identifiers.hpp>
#include <daemon/manager_document_reader.hpp>

namespace fs = std::filesystem;

namespace {

constexpr std::size_t max_history_limit = 100;
constexpr std::size_t max_history_offset = 10000;

btrfsbackup::daemon::SanitizedHistoryEntry sanitize_private_history(const btrfsbackup::config::Json& input) {
    if (!input.is_object() || input.value("schemaVersion", 0) != 2) {
        throw btrfsbackup::ValidationError("private history has an unsupported schema");
    }
    const std::string state = input.value("state", "unavailable");
    const std::string detailed_error = input.value("errorCode", "");
    return {
        .state = state,
        .error_code = detailed_error.empty() ? "" : (state == "cancelled" ? "backup.cancelled" : "backup.failed"),
        .source_name = input.value("currentSourceName", std::string{}),
        .target_name = input.value("targetName", std::string{}),
        .finished_at = input.value("finishedAt", std::string{}),
        .overall_progress = input.value("overallProgress", -1),
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
        if (name != "last.json" && entry.path().extension() == ".json" && btrfsbackup::daemon::manager_regular_file_without_symlink(entry)) {
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

namespace btrfsbackup::daemon {

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
    SanitizedHistoryPage result;
    if (offset >= files.size()) {
        return result;
    }
    const std::size_t end = std::min(files.size(), offset + limit);
    for (std::size_t index = offset; index < end; ++index) {
        result.entries.push_back(sanitize_private_history(read_manager_json_document(files[index])));
    }
    return result;
}

std::optional<SanitizedHistoryEntry> HistoryQueryService::get_last_sanitized(
    const std::string& profile_id
) const {
    validate_profile_id(profile_id);
    const fs::path last = history_root_ / profile_id / "last.json";
    if (!manager_regular_file_if_present(last)) {
        return std::nullopt;
    }
    return sanitize_private_history(read_manager_json_document(last));
}

} // namespace btrfsbackup::daemon
