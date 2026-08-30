// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <state/query/RunState.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include <core/Errors.hpp>
#include <core/Identifiers.hpp>

namespace fs = std::filesystem;

namespace {

constexpr fs::perms private_state_file_permissions =
    fs::perms::owner_read | fs::perms::owner_write;
constexpr fs::perms private_state_directory_permissions =
    private_state_file_permissions | fs::perms::owner_exec;

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
    LocalDate today,
    const btrfsbackup::config::LuksUuid& target_luks_uuid,
    const btrfsbackup::config::ConfigurationFingerprint& config_fingerprint
) {
    fs::path state_file = profile_state_dir / "last-success";
    std::error_code ec;
    if (!fs::is_regular_file(state_file, ec) || ec) {
        return false;
    }

    std::map<std::string, std::string> values = read_state_file(state_file);
    return get_value(values, "date") == format_local_date(today) &&
        lower(get_value(values, "target_luks_uuid")) == lower(target_luks_uuid.value()) &&
        get_value(values, "config_fingerprint") == config_fingerprint.value();
}

void write_success_state(
    IAtomicDocumentWriter& files,
    const fs::path& profile_state_dir,
    const SuccessState& state
) {
    require_non_empty(state.profile_name, "profile_name");
    if (state.source_count < 0) {
        throw ValidationError("source_count must be non-negative");
    }

    files.ensure_directory(profile_state_dir, private_state_directory_permissions);

    std::ostringstream content;
    content << "date=" << format_local_date(state.date) << '\n'
            << "timestamp=" << format_local_timestamp(state.timestamp) << '\n'
            << "run_id=" << state.run_id.value() << '\n'
            << "profile_id=" << state.profile_id.value() << '\n'
            << "profile_name=" << state.profile_name << '\n'
            << "source_count=" << state.source_count << '\n'
            << "target_luks_uuid=" << state.target_luks_uuid.value() << '\n'
            << "config_fingerprint=" << state.config_fingerprint.value() << '\n';

    files.write_atomically(
        profile_state_dir / "last-success",
        content.str(),
        private_state_file_permissions
    );
}

fs::path cancel_request_path(const fs::path& profile_state_dir) {
    return profile_state_dir / "cancel-request";
}

fs::path active_run_path(const fs::path& profile_state_dir) {
    return profile_state_dir / "active-run";
}

void write_active_run(
    IAtomicDocumentWriter& files,
    const fs::path& profile_state_dir,
    const RunId& run_id
) {
    files.ensure_directory(profile_state_dir, private_state_directory_permissions);
    files.write_atomically(
        active_run_path(profile_state_dir),
        "run_id=" + std::string(run_id.value()) + "\n",
        private_state_file_permissions
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
    IDurableDocumentRemover& files,
    const fs::path& profile_state_dir,
    const RunId& run_id
) {
    const std::optional<RunId> current = active_run(profile_state_dir);
    if (current.has_value() && *current == run_id) {
        files.remove_durably(active_run_path(profile_state_dir));
    }
}

void write_cancel_request(
    IAtomicDocumentWriter& files,
    const fs::path& profile_state_dir,
    const RunId& run_id
) {
    files.ensure_directory(profile_state_dir, private_state_directory_permissions);
    files.write_atomically(
        cancel_request_path(profile_state_dir),
        "run_id=" + std::string(run_id.value()) + "\n",
        private_state_file_permissions
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

void clear_cancel_request(IDurableDocumentRemover& files, const fs::path& profile_state_dir) {
    files.remove_durably(cancel_request_path(profile_state_dir));
}

void clear_cancel_request(
    IDurableDocumentRemover& files,
    const fs::path& profile_state_dir,
    const RunId& run_id
) {
    if (cancel_requested(profile_state_dir, run_id)) {
        clear_cancel_request(files, profile_state_dir);
    }
}

fs::path pending_marker_path(const fs::path& profile_state_dir, const SourceId& source_id) {
    return profile_state_dir / ("pending-" + std::string(source_id.value()));
}

void write_pending_marker(
    IAtomicDocumentWriter& files,
    const fs::path& profile_state_dir,
    const btrfsbackup::backup::PendingMarker& marker
) {
    require_absolute_path(marker.local_snapshot_path.string(), "local_snapshot_path");
    require_absolute_path(marker.final_snapshot_path.string(), "final_snapshot_path");

    files.ensure_directory(profile_state_dir, private_state_directory_permissions);

    std::ostringstream content;
    content << "source_name=" << marker.source_id.value() << '\n'
            << "local_snapshot_path=" << marker.local_snapshot_path.string() << '\n'
            << "final_snapshot_path=" << marker.final_snapshot_path.string() << '\n'
            << "run_id=" << marker.run_id.value() << '\n'
            << "timestamp=" << format_utc_iso_timestamp(marker.timestamp) << '\n';

    files.write_atomically(
        pending_marker_path(profile_state_dir, marker.source_id),
        content.str(),
        private_state_file_permissions
    );
}

std::string read_pending_marker_field(const fs::path& marker_path, const std::string& field) {
    std::map<std::string, std::string> values = read_state_file(marker_path);
    return get_value(values, field);
}

void clear_pending_marker(IDurableDocumentRemover& files, const fs::path& marker_path) {
    files.remove_durably(marker_path);
}

} // namespace btrfsbackup::state
