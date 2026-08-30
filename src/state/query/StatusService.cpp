// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/query/StatusService.hpp>

#include <algorithm>

#include <core/Errors.hpp>
#include <core/Identifiers.hpp>
#include <state/document/BoundedDocumentReader.hpp>
#include <state/document/LatestRunHistoryDocumentReader.hpp>
#include <state/document/RunStatusDocumentCodec.hpp>

namespace fs = std::filesystem;

namespace {

bool regular_file_without_symlink(const fs::path& path) {
    std::error_code error;
    return fs::symlink_status(path, error).type() == fs::file_type::regular && !error;
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
                if (regular_file_without_symlink(current))
                    paths.push_back(std::move(current));
            }
        }
        std::sort(paths.begin(), paths.end());
        if (paths.empty())
            throw ValidationError("no status files found under " + status_root.string());
    } else {
        validate_profile_id(profile_id);
        fs::path path = status_root / profile_id / "current.json";
        if (!regular_file_without_symlink(path)) {
            const document::LatestRunHistoryDocumentReader history_reader;
            std::optional<document::PrivateRunHistoryDocument> latest =
                history_reader.read(history_root / profile_id);
            if (latest.has_value()) {
                return {{
                    .status = std::move(latest->history),
                    .content = std::move(latest->content),
                    .source = std::move(latest->source),
                }};
            }
        }
        paths.push_back(std::move(path));
    }
    std::vector<StatusDocument> documents;
    const document::RunStatusDocumentCodec codec;
    const document::BoundedDocumentReader reader;
    for (const fs::path& path : paths) {
        std::string content = reader.read(path, document::maximum_run_document_size);
        documents.push_back({.status = codec.parse_public(content), .content = std::move(content), .source = path});
    }
    return documents;
}

std::optional<StatusDocument> poll_status(const fs::path& status_root, const std::string& profile_id, const std::string& previous) {
    validate_profile_id(profile_id);
    fs::path path = status_root / profile_id / "current.json";
    if (!regular_file_without_symlink(path))
        return std::nullopt;
    const document::BoundedDocumentReader reader;
    std::string content = reader.read(path, document::maximum_run_document_size);
    if (content == previous)
        return std::nullopt;
    document::RunStatusDocumentCodec codec;
    return StatusDocument{.status = codec.parse_public(content), .content = std::move(content), .source = path};
}

} // namespace btrfsbackup::state
