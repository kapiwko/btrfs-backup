// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/StatusService.hpp>

#include <algorithm>
#include <fstream>

#include <core/Errors.hpp>
#include <core/Identifiers.hpp>

namespace fs = std::filesystem;

namespace {

bool readable_file(const fs::path& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec) && !ec && std::ifstream(path).good();
}

btrfsbackup::state::StatusDocument read_document(const fs::path& path) {
    std::ifstream stream(path);
    if (!stream)
        throw btrfsbackup::ValidationError("cannot read " + path.string());
    std::string content{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    return {.data = btrfsbackup::config::json::Json::parse(content), .content = std::move(content), .source = path};
}

void validate_status_api(const btrfsbackup::config::json::Json& data) {
    if (!data.is_object() || !data.contains("schemaVersion") || data.at("schemaVersion") != 3) {
        throw btrfsbackup::ValidationError("status JSON has unsupported schemaVersion");
    }
    for (const char* field : {"state", "errorCode", "sourceName", "targetName", "speedBps", "etaSeconds", "sourceProgress", "overallProgress", "progressAccuracy"}) {
        if (!data.contains(field)) {
            throw btrfsbackup::ValidationError(std::string("status JSON is missing required field: ") + field);
        }
    }
}

} // namespace

namespace btrfsbackup::state {

std::vector<StatusDocument> get_statuses(
    const fs::path& status_root,
    const fs::path& history_root,
    const std::string& profile_id,
    bool all
) {
    std::vector<fs::path> paths;
    if (all) {
        std::error_code ec;
        if (fs::is_directory(status_root, ec) && !ec) {
            for (const auto& entry : fs::directory_iterator(status_root, ec)) {
                if (ec)
                    break;
                fs::path current = entry.path() / "current.json";
                if (readable_file(current))
                    paths.push_back(std::move(current));
            }
        }
        std::sort(paths.begin(), paths.end());
        if (paths.empty())
            throw ValidationError("no status files found under " + status_root.string());
    } else {
        validate_profile_id(profile_id);
        fs::path path = status_root / profile_id / "current.json";
        if (!readable_file(path) && readable_file(history_root / profile_id / "last.json")) {
            path = history_root / profile_id / "last.json";
        }
        paths.push_back(std::move(path));
    }
    std::vector<StatusDocument> documents;
    for (const auto& path : paths)
        documents.push_back(read_document(path));
    return documents;
}

std::optional<StatusDocument> poll_status(const fs::path& status_root, const std::string& profile_id, const std::string& previous) {
    validate_profile_id(profile_id);
    fs::path path = status_root / profile_id / "current.json";
    if (!readable_file(path))
        return std::nullopt;
    StatusDocument document = read_document(path);
    if (document.content == previous)
        return std::nullopt;
    validate_status_api(document.data);
    return document;
}

} // namespace btrfsbackup::state
