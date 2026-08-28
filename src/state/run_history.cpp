// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/run_history.hpp>

#include <algorithm>
#include <fstream>

#include <config/model/json.hpp>
#include <core/errors.hpp>
#include <core/file_permissions.hpp>
#include <core/identifiers.hpp>
#include <state/status_writer.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::state {

void write_history_entry(IAtomicDocumentWriter& files, const fs::path& history_root, const RunStatus& status) {
    const std::string content = dump_status_json(status);
    const fs::path directory = history_root / status.profile_id.value();
    const fs::path run_path = directory / (std::string(status.run_id.value()) + ".json");
    const fs::path last_path = directory / "last.json";

    files.ensure_directory(history_root, private_directory_permissions);
    files.ensure_directory(directory, private_directory_permissions);
    files.write_atomically(run_path, content, private_file_permissions);
    files.write_atomically(last_path, content, private_file_permissions);
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
        if (entry.is_regular_file(error) && !error && name != "last.json" && entry.path().extension() == ".json") {
            paths.push_back(entry.path());
        }
        error.clear();
    }
    std::sort(paths.rbegin(), paths.rend());
    if (paths.size() > limit) {
        paths.resize(limit);
    }

    std::vector<StatusDocument> documents;
    for (const fs::path& path : paths) {
        std::ifstream stream(path);
        if (!stream) {
            throw ValidationError("cannot read " + path.string());
        }
        std::string content{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        documents.push_back({.data = btrfsbackup::config::Json::parse(content), .content = std::move(content), .source = path});
    }
    return documents;
}

} // namespace btrfsbackup::state
