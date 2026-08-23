#include <btrfsbackup/run_state_command.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <ostream>
#include <string>
#include <vector>

#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/run_state.hpp>

namespace fs = std::filesystem;

namespace {

std::string arg_value(const std::vector<std::string>& args, std::size_t& index, const std::string& option) {
    if (index + 1 >= args.size()) {
        throw btrfsbackup::ValidationError(option + " requires a value");
    }
    return args[++index];
}

void require_non_empty(const std::string& value, const char* field) {
    if (value.empty()) {
        throw btrfsbackup::ValidationError(std::string(field) + " is required");
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
