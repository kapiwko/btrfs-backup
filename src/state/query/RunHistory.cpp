// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/query/RunHistory.hpp>

#include <algorithm>

#include <core/Errors.hpp>
#include <core/Identifiers.hpp>
#include <state/document/BoundedDocumentReader.hpp>
#include <state/document/LatestRunHistoryDocumentReader.hpp>
#include <state/document/RunStatusDocumentCodec.hpp>
#include <state/persistence/StatusWriter.hpp>

namespace fs = std::filesystem;

namespace {

constexpr fs::perms private_history_file_permissions =
    fs::perms::owner_read | fs::perms::owner_write;
constexpr fs::perms private_history_directory_permissions =
    private_history_file_permissions | fs::perms::owner_exec;

} // namespace

namespace btrfsbackup::state {

void write_history_entry(IAtomicDocumentWriter& files, const fs::path& history_root, const RunStatus& status) {
    const std::string content = dump_status_json(status);
    const fs::path directory = history_root / status.profile_id.value();
    const fs::path run_path = directory / (std::string(status.run_id.value()) + ".json");
    const fs::path last_path = directory / "last.json";

    files.ensure_directory(history_root, private_history_directory_permissions);
    files.ensure_directory(directory, private_history_directory_permissions);
    // The run-specific document is authoritative. last.json is a rebuildable cache.
    files.write_atomically(run_path, content, private_history_file_permissions);
    files.write_atomically(last_path, content, private_history_file_permissions);
}

std::vector<StatusDocument> get_status_history(
    const fs::path& history_root,
    const std::string& profile_id,
    std::size_t limit
) {
    validate_profile_id(profile_id);
    const fs::path directory = history_root / profile_id;
    std::error_code error;
    if (!fs::is_directory(directory, error) || error) {
        return {};
    }

    std::vector<fs::path> paths;
    for (const auto& entry : fs::directory_iterator(directory, error)) {
        if (error) {
            break;
        }
        const std::string name = entry.path().filename().string();
        if (entry.symlink_status(error).type() == fs::file_type::regular && !error &&
            name != "last.json" && entry.path().extension() == ".json") {
            paths.push_back(entry.path());
        }
        error.clear();
    }
    std::sort(paths.rbegin(), paths.rend());
    if (paths.size() > limit) {
        paths.resize(limit);
    }

    std::vector<StatusDocument> documents;
    const document::RunStatusDocumentCodec codec;
    const document::BoundedDocumentReader reader;
    for (const fs::path& path : paths) {
        std::string content = reader.read(path, document::maximum_run_document_size);
        documents.push_back({.status = codec.parse_private(content), .content = std::move(content), .source = path});
    }
    return documents;
}

} // namespace btrfsbackup::state
