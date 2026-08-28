// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/run_state.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include <core/errors.hpp>
#include <core/identifiers.hpp>

namespace fs = std::filesystem;

namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::map<std::string, std::string> read_state_file(const fs::path& path) {
    std::ifstream stream(path);
    std::map<std::string, std::string> values;
    std::string line;
    while (std::getline(stream, line)) {
        std::size_t eq = line.find('=');
        if (eq != std::string::npos) {
            values.emplace(line.substr(0, eq), line.substr(eq + 1));
        }
    }
    return values;
}

std::string get_value(const std::map<std::string, std::string>& values, const std::string& key) {
    auto it = values.find(key);
    return it == values.end() ? "" : it->second;
}

void require_non_empty(const std::string& value, const char* field) {
    if (value.empty()) {
        throw btrfsbackup::ValidationError(std::string(field) + " is required");
    }
}

void require_absolute_path(const std::string& value, const char* field) {
    require_non_empty(value, field);
    if (value.front() != '/') {
        throw btrfsbackup::ValidationError(std::string(field) + " must be an absolute path");
    }
}

} // namespace

namespace btrfsbackup::state {

bool last_success_matches(
    const fs::path& profile_state_dir,
    const std::string& today,
    const std::string& target_luks_uuid,
    const std::string& config_fingerprint
) {
    fs::path state_file = profile_state_dir / "last-success";
    std::error_code ec;
    if (!fs::is_regular_file(state_file, ec) || ec) {
        return false;
    }

    std::map<std::string, std::string> values = read_state_file(state_file);
    return get_value(values, "date") == today && lower(get_value(values, "target_luks_uuid")) == lower(target_luks_uuid) && get_value(values, "config_fingerprint") == config_fingerprint;
}

void write_success_state(
    IDurableFileOperations& files,
    const fs::path& profile_state_dir,
    const SuccessState& state
) {
    validate_profile_id(state.profile_id);
    validate_run_id(state.run_id);
    require_non_empty(state.date, "date");
    require_non_empty(state.timestamp, "timestamp");
    require_non_empty(state.profile_name, "profile_name");
    require_non_empty(state.target_luks_uuid, "target_luks_uuid");
    require_non_empty(state.config_fingerprint, "config_fingerprint");
    if (state.source_count < 0) {
        throw ValidationError("source_count must be non-negative");
    }

    files.ensure_directory(profile_state_dir, private_directory_permissions);

    std::ostringstream content;
    content << "date=" << state.date << '\n'
            << "timestamp=" << state.timestamp << '\n'
            << "run_id=" << state.run_id << '\n'
            << "profile_id=" << state.profile_id << '\n'
            << "profile_name=" << state.profile_name << '\n'
            << "source_count=" << state.source_count << '\n'
            << "target_luks_uuid=" << state.target_luks_uuid << '\n'
            << "config_fingerprint=" << state.config_fingerprint << '\n';

    files.write_atomically(profile_state_dir / "last-success", content.str(), private_file_permissions);
}

fs::path cancel_request_path(const fs::path& profile_state_dir) {
    return profile_state_dir / "cancel-request";
}

fs::path active_run_path(const fs::path& profile_state_dir) {
    return profile_state_dir / "active-run";
}

void write_active_run(
    IDurableFileOperations& files,
    const fs::path& profile_state_dir,
    const RunId& run_id
) {
    files.ensure_directory(profile_state_dir, private_directory_permissions);
    files.write_atomically(
        active_run_path(profile_state_dir),
        "run_id=" + std::string(run_id.value()) + "\n",
        private_file_permissions
    );
}

std::optional<RunId> active_run(const fs::path& profile_state_dir) {
    const fs::path path = active_run_path(profile_state_dir);
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || ec) {
        return std::nullopt;
    }
    const std::string run_id = get_value(read_state_file(path), "run_id");
    return run_id.empty() ? std::nullopt : std::optional<RunId>{RunId{run_id}};
}

void clear_active_run(
    IDurableFileOperations& files,
    const fs::path& profile_state_dir,
    const RunId& run_id
) {
    const std::optional<RunId> current = active_run(profile_state_dir);
    if (current.has_value() && *current == run_id) {
        files.remove_durably(active_run_path(profile_state_dir));
    }
}

void write_cancel_request(
    IDurableFileOperations& files,
    const fs::path& profile_state_dir,
    const RunId& run_id
) {
    files.ensure_directory(profile_state_dir, private_directory_permissions);
    files.write_atomically(
        cancel_request_path(profile_state_dir),
        "run_id=" + std::string(run_id.value()) + "\n",
        private_file_permissions
    );
}

bool cancel_requested(const fs::path& profile_state_dir) {
    std::error_code ec;
    return fs::is_regular_file(cancel_request_path(profile_state_dir), ec) && !ec;
}

bool cancel_requested(const fs::path& profile_state_dir, const RunId& run_id) {
    const fs::path path = cancel_request_path(profile_state_dir);
    std::error_code ec;
    if (!fs::is_regular_file(path, ec) || ec) {
        return false;
    }
    return get_value(read_state_file(path), "run_id") == run_id.value();
}

void clear_cancel_request(IDurableFileOperations& files, const fs::path& profile_state_dir) {
    files.remove_durably(cancel_request_path(profile_state_dir));
}

void clear_cancel_request(
    IDurableFileOperations& files,
    const fs::path& profile_state_dir,
    const RunId& run_id
) {
    if (cancel_requested(profile_state_dir, run_id)) {
        clear_cancel_request(files, profile_state_dir);
    }
}

fs::path pending_marker_path(const fs::path& profile_state_dir, const std::string& source_name) {
    validate_identifier(source_name, "source_name");
    return profile_state_dir / ("pending-" + source_name);
}

void write_pending_marker(
    IDurableFileOperations& files,
    const fs::path& profile_state_dir,
    const btrfsbackup::backup::PendingMarker& marker
) {
    validate_identifier(marker.source_name, "source_name");
    validate_run_id(marker.run_id);
    require_absolute_path(marker.local_snapshot_path, "local_snapshot_path");
    require_absolute_path(marker.final_snapshot_path, "final_snapshot_path");
    require_non_empty(marker.timestamp, "timestamp");

    files.ensure_directory(profile_state_dir, private_directory_permissions);

    std::ostringstream content;
    content << "source_name=" << marker.source_name << '\n'
            << "local_snapshot_path=" << marker.local_snapshot_path << '\n'
            << "final_snapshot_path=" << marker.final_snapshot_path << '\n'
            << "run_id=" << marker.run_id << '\n'
            << "timestamp=" << marker.timestamp << '\n';

    files.write_atomically(
        pending_marker_path(profile_state_dir, marker.source_name),
        content.str(),
        private_file_permissions
    );
}

std::string read_pending_marker_field(const fs::path& marker_path, const std::string& field) {
    std::map<std::string, std::string> values = read_state_file(marker_path);
    return get_value(values, field);
}

void clear_pending_marker(IDurableFileOperations& files, const fs::path& marker_path) {
    files.remove_durably(marker_path);
}

} // namespace btrfsbackup::state
