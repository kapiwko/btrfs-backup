// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/persistence/StatusWriter.hpp>

#include <filesystem>
#include <string>
#include <variant>

#include <core/Errors.hpp>
#include <core/Identifiers.hpp>
#include <core/RuntimeTime.hpp>
#include <config/json/JsonIo.hpp>
#include <state/document/RunStatusDocumentCodec.hpp>

namespace fs = std::filesystem;

namespace {

constexpr fs::perms public_status_file_permissions =
    fs::perms::owner_read | fs::perms::owner_write | fs::perms::group_read | fs::perms::others_read;
constexpr fs::perms public_status_directory_permissions =
    public_status_file_permissions | fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec;

void require_non_empty(const std::string& value, const char* field) {
    if (value.empty()) {
        throw btrfsbackup::ValidationError(std::string(field) + " is required");
    }
}

void validate_status(const btrfsbackup::state::RunStatus& status) {
    require_non_empty(status.profile_name, "profileName");
    btrfsbackup::state::validate_run_status(status);
}

void prepare_public_parent(btrfsbackup::state::IAtomicDocumentWriter& files, const fs::path& path) {
    files.ensure_directory(path.parent_path(), public_status_directory_permissions);
}

} // namespace

namespace btrfsbackup::state {

btrfsbackup::config::json::Json build_status_json(const RunStatus& status) {
    validate_status(status);
    const document::RunStatusDocumentCodec codec;
    return btrfsbackup::config::json::Json::parse(codec.serialize_private(document::make_private_history(status)));
}

std::string dump_status_json(const RunStatus& status) {
    validate_status(status);
    return document::RunStatusDocumentCodec{}.serialize_private(document::make_private_history(status));
}

btrfsbackup::config::json::Json build_public_status_json(const RunStatus& status) {
    validate_status(status);
    const document::RunStatusDocumentCodec codec;
    btrfsbackup::config::json::Json result = btrfsbackup::config::json::Json::parse(
        codec.serialize_public(document::make_public_status(status))
    );
    result["sourceIndex"] = status.source_index;
    result["sourceCount"] = status.source_count;
    result["startedAt"] = format_utc_iso_timestamp(status.started_at);
    result["updatedAt"] = format_utc_iso_timestamp(status.updated_at);
    return result;
}

std::string dump_public_status_json(const RunStatus& status) {
    return btrfsbackup::config::json::dump_json(build_public_status_json(status));
}

void write_current_status(
    IAtomicDocumentWriter& files,
    const fs::path& status_root,
    const RunStatus& status
) {
    std::string content = dump_public_status_json(status);
    fs::path path = status_root / status.profile_id.value() / "current.json";
    prepare_public_parent(files, path);
    files.write_atomically(path, content, public_status_file_permissions);
}

} // namespace btrfsbackup::state
