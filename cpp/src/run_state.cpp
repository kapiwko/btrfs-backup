#include <btrfsbackup/run_state.hpp>

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/file_io.hpp>
#include <btrfsbackup/identifiers.hpp>

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

namespace btrfsbackup {

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
    return get_value(values, "date") == today
        && lower(get_value(values, "target_luks_uuid")) == lower(target_luks_uuid)
        && get_value(values, "config_fingerprint") == config_fingerprint;
}

void write_success_state(const fs::path& profile_state_dir, const SuccessState& state) {
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

    fs::create_directories(profile_state_dir);
    chmod(profile_state_dir.c_str(), 0700);

    std::ostringstream content;
    content << "date=" << state.date << '\n'
            << "timestamp=" << state.timestamp << '\n'
            << "run_id=" << state.run_id << '\n'
            << "profile_id=" << state.profile_id << '\n'
            << "profile_name=" << state.profile_name << '\n'
            << "source_count=" << state.source_count << '\n'
            << "target_luks_uuid=" << state.target_luks_uuid << '\n'
            << "config_fingerprint=" << state.config_fingerprint << '\n';

    atomic_write(profile_state_dir / "last-success", content.str(), 0600);
    fsync_dir(profile_state_dir);
}

fs::path cancel_request_path(const fs::path& profile_state_dir) {
    return profile_state_dir / "cancel-request";
}

void write_cancel_request(const fs::path& profile_state_dir) {
    fs::create_directories(profile_state_dir);
    chmod(profile_state_dir.c_str(), 0700);
    atomic_write(cancel_request_path(profile_state_dir), "requested=1\n", 0600);
    fsync_dir(profile_state_dir);
}

bool cancel_requested(const fs::path& profile_state_dir) {
    std::error_code ec;
    return fs::is_regular_file(cancel_request_path(profile_state_dir), ec) && !ec;
}

void clear_cancel_request(const fs::path& profile_state_dir) {
    std::error_code ec;
    fs::remove(cancel_request_path(profile_state_dir), ec);
    fsync_dir(profile_state_dir);
}

fs::path pending_marker_path(const fs::path& profile_state_dir, const std::string& source_name) {
    validate_identifier(source_name, "source_name");
    return profile_state_dir / ("pending-" + source_name);
}

void write_pending_marker(const fs::path& profile_state_dir, const PendingMarker& marker) {
    validate_identifier(marker.source_name, "source_name");
    validate_run_id(marker.run_id);
    require_absolute_path(marker.local_snapshot_path, "local_snapshot_path");
    require_non_empty(marker.timestamp, "timestamp");

    fs::create_directories(profile_state_dir);
    chmod(profile_state_dir.c_str(), 0700);

    std::ostringstream content;
    content << "source_name=" << marker.source_name << '\n'
            << "local_snapshot_path=" << marker.local_snapshot_path << '\n'
            << "run_id=" << marker.run_id << '\n'
            << "timestamp=" << marker.timestamp << '\n';

    atomic_write(pending_marker_path(profile_state_dir, marker.source_name), content.str(), 0600);
    fsync_dir(profile_state_dir);
}

std::string read_pending_marker_field(const fs::path& marker_path, const std::string& field) {
    std::map<std::string, std::string> values = read_state_file(marker_path);
    return get_value(values, field);
}

void clear_pending_marker(const fs::path& marker_path, const fs::path& profile_state_dir) {
    std::error_code ec;
    fs::remove(marker_path, ec);
    fsync_dir(profile_state_dir);
}

} // namespace btrfsbackup
