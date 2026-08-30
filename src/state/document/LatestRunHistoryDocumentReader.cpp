// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/document/LatestRunHistoryDocumentReader.hpp>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include <core/Errors.hpp>
#include <state/document/BoundedDocumentReader.hpp>

namespace fs = std::filesystem;

namespace {

bool regular_file_without_symlink(const fs::path& path) {
    std::error_code error;
    return fs::symlink_status(path, error).type() == fs::file_type::regular && !error;
}

std::vector<fs::path> authoritative_history_paths(const fs::path& directory) {
    std::error_code error;
    if (!fs::is_directory(directory, error) || error) {
        return {};
    }

    std::vector<fs::path> paths;
    for (const fs::directory_entry& entry : fs::directory_iterator(directory, error)) {
        if (error) {
            break;
        }
        const fs::path& path = entry.path();
        if (path.filename() != "last.json" && path.extension() == ".json" &&
            regular_file_without_symlink(path)) {
            paths.push_back(path);
        }
    }
    if (error) {
        throw btrfsbackup::ValidationError(
            "cannot enumerate history directory " + directory.string()
        );
    }
    std::sort(paths.rbegin(), paths.rend());
    return paths;
}

} // namespace

namespace btrfsbackup::state {
namespace document {

std::optional<PrivateRunHistoryDocument> LatestRunHistoryDocumentReader::read(
    const fs::path& history_directory,
    std::size_t maximum_size
) const {
    const BoundedDocumentReader reader;
    const RunStatusDocumentCodec codec;
    const auto read_document = [&](const fs::path& path) {
        std::string content = reader.read(path, maximum_size);
        return PrivateRunHistoryDocument{
            .history = codec.parse_private(content),
            .content = std::move(content),
            .source = path,
        };
    };

    const std::vector<fs::path> authoritative_paths =
        authoritative_history_paths(history_directory);
    const fs::path cache_path = history_directory / "last.json";
    if (authoritative_paths.empty()) {
        if (!regular_file_without_symlink(cache_path)) {
            return std::nullopt;
        }
        return read_document(cache_path);
    }

    const fs::path& authoritative_path = authoritative_paths.front();
    if (regular_file_without_symlink(cache_path)) {
        try {
            PrivateRunHistoryDocument cached = read_document(cache_path);
            if (cached.history.run_id.value() == authoritative_path.stem().string()) {
                return cached;
            }
        } catch (const ValidationError&) {
            // A broken cache must not hide the authoritative history record.
        }
    }

    PrivateRunHistoryDocument authoritative = read_document(authoritative_path);
    if (authoritative.history.run_id.value() != authoritative_path.stem().string()) {
        throw ValidationError(
            "history document runId does not match filename: " + authoritative_path.string()
        );
    }
    return authoritative;
}

} // namespace document
} // namespace btrfsbackup::state
