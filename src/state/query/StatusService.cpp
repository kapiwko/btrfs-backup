// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/query/StatusService.hpp>

#include <algorithm>
#include <fstream>

#include <core/Identifiers.hpp>
#include <core/Errors.hpp>
#include <state/document/RunStatusDocumentCodec.hpp>

namespace fs = std::filesystem;

namespace {

bool readable_file(const fs::path& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec) && !ec && std::ifstream(path).good();
}

std::string read_document(const fs::path& path) {
    std::ifstream stream(path);
    if (!stream)
        throw btrfsbackup::ValidationError("cannot read " + path.string());
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

} // namespace

namespace btrfsbackup::state {

std::vector<StatusDocument> get_statuses(
    const fs::path& status_root,
    const fs::path& history_root,
    const std::string& profile_id,
    bool all
) {
    std::vector<std::pair<fs::path, bool>> paths;
    if (all) {
        std::error_code ec;
        if (fs::is_directory(status_root, ec) && !ec) {
            for (const auto& entry : fs::directory_iterator(status_root, ec)) {
                if (ec)
                    break;
                fs::path current = entry.path() / "current.json";
                if (readable_file(current))
                    paths.emplace_back(std::move(current), true);
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
        const bool is_public = path.filename() == "current.json";
        paths.emplace_back(std::move(path), is_public);
    }
    std::vector<StatusDocument> documents;
    const document::RunStatusDocumentCodec codec;
    for (const auto& [path, is_public] : paths) {
        std::string content = read_document(path);
        if (is_public) {
            documents.push_back({.status = codec.parse_public(content), .content = std::move(content), .source = path});
        } else {
            documents.push_back({.status = codec.parse_private(content), .content = std::move(content), .source = path});
        }
    }
    return documents;
}

std::optional<StatusDocument> poll_status(const fs::path& status_root, const std::string& profile_id, const std::string& previous) {
    validate_profile_id(profile_id);
    fs::path path = status_root / profile_id / "current.json";
    if (!readable_file(path))
        return std::nullopt;
    std::string content = read_document(path);
    if (content == previous)
        return std::nullopt;
    document::RunStatusDocumentCodec codec;
    return StatusDocument{.status = codec.parse_public(content), .content = std::move(content), .source = path};
}

} // namespace btrfsbackup::state
