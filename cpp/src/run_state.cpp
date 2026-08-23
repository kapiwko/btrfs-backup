#include <btrfsbackup/run_state.hpp>

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <ostream>
#include <sstream>
#include <string>
#include <vector>

#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/file_io.hpp>
#include <btrfsbackup/identifiers.hpp>

namespace fs = std::filesystem;

namespace {

std::string arg_value(const std::vector<std::string>& args, std::size_t& index, const std::string& option) {
    if (index + 1 >= args.size()) {
        throw btrfsbackup::ValidationError(option + " requires a value");
    }
    return args[++index];
}

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

int parse_int(const std::string& option, const std::string& value) {
    if (value.empty() || !std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isdigit(c); })) {
        throw btrfsbackup::ValidationError(option + " must be a number");
    }
    return std::stoi(value);
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

void migrate_legacy_state(const fs::path& state_dir, const fs::path& profile_state_dir) {
    fs::create_directories(state_dir);
    chmod(state_dir.c_str(), 0755);
    fs::create_directories(profile_state_dir);
    chmod(profile_state_dir.c_str(), 0700);

    std::error_code ec;
    fs::path legacy_success = state_dir / "last-success";
    fs::path profile_success = profile_state_dir / "last-success";
    if (fs::is_regular_file(legacy_success, ec) && !fs::exists(profile_success, ec)) {
        fs::rename(legacy_success, profile_success);
    }

    for (const auto& entry : fs::directory_iterator(state_dir, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file(ec)) {
            continue;
        }
        std::string name = entry.path().filename().string();
        if (name.rfind("pending-", 0) != 0) {
            continue;
        }
        fs::path target = profile_state_dir / name;
        if (fs::exists(target, ec)) {
            continue;
        }
        fs::rename(entry.path(), target);
    }

    fsync_dir(state_dir);
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

void command_check_last_success(const std::vector<std::string>& args, std::ostream& output) {
    fs::path profile_state_dir;
    std::string today;
    std::string target_luks_uuid;
    std::string config_fingerprint;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--profile-state-dir") {
            profile_state_dir = arg_value(args, i, arg);
        } else if (arg == "--today") {
            today = arg_value(args, i, arg);
        } else if (arg == "--target-luks-uuid") {
            target_luks_uuid = arg_value(args, i, arg);
        } else if (arg == "--config-fingerprint") {
            config_fingerprint = arg_value(args, i, arg);
        } else {
            throw ValidationError("unknown check-last-success option: " + arg);
        }
    }

    if (profile_state_dir.empty()) {
        throw ValidationError("check-last-success requires --profile-state-dir");
    }
    require_non_empty(today, "today");
    require_non_empty(target_luks_uuid, "target_luks_uuid");
    require_non_empty(config_fingerprint, "config_fingerprint");

    output << (last_success_matches(profile_state_dir, today, target_luks_uuid, config_fingerprint) ? "yes\n" : "no\n");
}

void command_write_success_state(const std::vector<std::string>& args) {
    fs::path profile_state_dir;
    SuccessState state;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--profile-state-dir") {
            profile_state_dir = arg_value(args, i, arg);
        } else if (arg == "--date") {
            state.date = arg_value(args, i, arg);
        } else if (arg == "--timestamp") {
            state.timestamp = arg_value(args, i, arg);
        } else if (arg == "--run-id") {
            state.run_id = arg_value(args, i, arg);
        } else if (arg == "--profile-id") {
            state.profile_id = arg_value(args, i, arg);
        } else if (arg == "--profile-name") {
            state.profile_name = arg_value(args, i, arg);
        } else if (arg == "--source-count") {
            state.source_count = parse_int(arg, arg_value(args, i, arg));
        } else if (arg == "--target-luks-uuid") {
            state.target_luks_uuid = arg_value(args, i, arg);
        } else if (arg == "--config-fingerprint") {
            state.config_fingerprint = arg_value(args, i, arg);
        } else {
            throw ValidationError("unknown write-success-state option: " + arg);
        }
    }

    if (profile_state_dir.empty()) {
        throw ValidationError("write-success-state requires --profile-state-dir");
    }
    write_success_state(profile_state_dir, state);
}

void command_migrate_legacy_state(const std::vector<std::string>& args) {
    fs::path state_dir;
    fs::path profile_state_dir;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--state-dir") {
            state_dir = arg_value(args, i, arg);
        } else if (arg == "--profile-state-dir") {
            profile_state_dir = arg_value(args, i, arg);
        } else {
            throw ValidationError("unknown migrate-legacy-state option: " + arg);
        }
    }

    if (state_dir.empty()) {
        throw ValidationError("migrate-legacy-state requires --state-dir");
    }
    if (profile_state_dir.empty()) {
        throw ValidationError("migrate-legacy-state requires --profile-state-dir");
    }
    migrate_legacy_state(state_dir, profile_state_dir);
}

void command_write_pending_marker(const std::vector<std::string>& args) {
    fs::path profile_state_dir;
    PendingMarker marker;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--profile-state-dir") {
            profile_state_dir = arg_value(args, i, arg);
        } else if (arg == "--source-name") {
            marker.source_name = arg_value(args, i, arg);
        } else if (arg == "--local-snapshot-path") {
            marker.local_snapshot_path = arg_value(args, i, arg);
        } else if (arg == "--run-id") {
            marker.run_id = arg_value(args, i, arg);
        } else if (arg == "--timestamp") {
            marker.timestamp = arg_value(args, i, arg);
        } else {
            throw ValidationError("unknown write-pending-marker option: " + arg);
        }
    }

    if (profile_state_dir.empty()) {
        throw ValidationError("write-pending-marker requires --profile-state-dir");
    }
    write_pending_marker(profile_state_dir, marker);
}

void command_read_pending_marker(const std::vector<std::string>& args, std::ostream& output) {
    fs::path marker_path;
    std::string field = "local_snapshot_path";

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--marker") {
            marker_path = arg_value(args, i, arg);
        } else if (arg == "--field") {
            field = arg_value(args, i, arg);
        } else {
            throw ValidationError("unknown read-pending-marker option: " + arg);
        }
    }

    if (marker_path.empty()) {
        throw ValidationError("read-pending-marker requires --marker");
    }
    output << read_pending_marker_field(marker_path, field) << '\n';
}

void command_clear_pending_marker(const std::vector<std::string>& args) {
    fs::path marker_path;
    fs::path profile_state_dir;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--marker") {
            marker_path = arg_value(args, i, arg);
        } else if (arg == "--profile-state-dir") {
            profile_state_dir = arg_value(args, i, arg);
        } else {
            throw ValidationError("unknown clear-pending-marker option: " + arg);
        }
    }

    if (marker_path.empty()) {
        throw ValidationError("clear-pending-marker requires --marker");
    }
    if (profile_state_dir.empty()) {
        throw ValidationError("clear-pending-marker requires --profile-state-dir");
    }
    clear_pending_marker(marker_path, profile_state_dir);
}

} // namespace btrfsbackup
