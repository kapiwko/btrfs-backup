// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/status_query_service.hpp>

#include <string>
#include <utility>

#include <core/errors.hpp>
#include <core/identifiers.hpp>
#include <daemon/history_query_service.hpp>
#include <daemon/manager_document_reader.hpp>

namespace fs = std::filesystem;

namespace {

btrfsbackup::config::Json unavailable_status() {
    return {
        {"schemaVersion", 3},
        {"state", "unavailable"},
        {"errorCode", ""},
        {"sourceName", ""},
        {"targetName", ""},
        {"speedBps", 0},
        {"etaSeconds", -1},
        {"sourceProgress", -1},
        {"overallProgress", -1},
        {"progressAccuracy", "indeterminate"},
    };
}

btrfsbackup::config::Json sanitize_public_status(const btrfsbackup::config::Json& input) {
    if (!input.is_object() || input.value("schemaVersion", 0) != 3) {
        throw btrfsbackup::ValidationError("public status has an unsupported schema");
    }
    btrfsbackup::config::Json result = unavailable_status();
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
        result[field] = input.at(field);
    }
    return result;
}

} // namespace

namespace btrfsbackup::daemon {

StatusQueryService::StatusQueryService(
    fs::path status_root,
    const HistoryQueryService& history
)
    : status_root_(std::move(status_root)), history_(history) {
}

btrfsbackup::config::Json StatusQueryService::get_status(const std::string& profile_id) const {
    validate_profile_id(profile_id);
    const fs::path current = status_root_ / profile_id / "current.json";
    if (manager_regular_file_if_present(current)) {
        return sanitize_public_status(read_manager_json_document(current));
    }

    const std::optional<btrfsbackup::config::Json> last = history_.get_last_sanitized(profile_id);
    if (last.has_value()) {
        btrfsbackup::config::Json result = unavailable_status();
        for (const char* field : {"state", "errorCode", "sourceName", "targetName", "overallProgress"}) {
            result[field] = last->at(field);
        }
        return result;
    }
    return unavailable_status();
}

} // namespace btrfsbackup::daemon
