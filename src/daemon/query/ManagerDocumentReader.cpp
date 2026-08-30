// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/query/ManagerDocumentReader.hpp>

#include <string>

#include <core/Errors.hpp>
#include <state/document/BoundedDocumentReader.hpp>

namespace fs = std::filesystem;

namespace {

constexpr std::size_t max_document_bytes = 1024 * 1024;

} // namespace

namespace btrfsbackup::daemon::query {

std::string read_manager_document(const fs::path& path) {
    const btrfsbackup::state::document::BoundedDocumentReader reader;
    return reader.read(path, max_document_bytes);
}

btrfsbackup::config::json::Json read_manager_json_document(const fs::path& path) {
    try {
        return btrfsbackup::config::json::Json::parse(read_manager_document(path));
    } catch (const std::exception& error) {
        throw ValidationError("invalid manager JSON " + path.string() + ": " + error.what());
    }
}

bool manager_regular_file_without_symlink(const fs::directory_entry& entry) {
    std::error_code error;
    return entry.symlink_status(error).type() == fs::file_type::regular && !error;
}

bool manager_regular_file_if_present(const fs::path& path) {
    std::error_code error;
    const fs::file_status status = fs::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory) {
        return false;
    }
    if (error) {
        throw ValidationError("cannot inspect manager data file " + path.string());
    }
    return status.type() == fs::file_type::regular;
}

} // namespace btrfsbackup::daemon::query
