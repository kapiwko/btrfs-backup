#include <btrfsbackup/command/state_run_command.hpp>

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

namespace btrfsbackup::command {

void state_check_last_success(const std::vector<std::string>& args, std::ostream& output) {
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
            throw ValidationError("unknown state check-last-success option: " + arg);
        }
    }

    if (profile_state_dir.empty()) {
        throw ValidationError("state check-last-success requires --profile-state-dir");
    }
    require_non_empty(today, "today");
    require_non_empty(target_luks_uuid, "target_luks_uuid");
    require_non_empty(config_fingerprint, "config_fingerprint");

    output << (btrfsbackup::last_success_matches(profile_state_dir, today, target_luks_uuid, config_fingerprint) ? "yes\n" : "no\n");
}

void state_write_success(const std::vector<std::string>& args) {
    fs::path profile_state_dir;
    btrfsbackup::SuccessState state;

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
            throw ValidationError("unknown state write-success option: " + arg);
        }
    }

    if (profile_state_dir.empty()) {
        throw ValidationError("state write-success requires --profile-state-dir");
    }
    btrfsbackup::write_success_state(profile_state_dir, state);
}

void state_pending_write(const std::vector<std::string>& args) {
    fs::path profile_state_dir;
    btrfsbackup::PendingMarker marker;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--profile-state-dir") {
            profile_state_dir = arg_value(args, i, arg);
        } else if (arg == "--source-name") {
            marker.source_name = arg_value(args, i, arg);
        } else if (arg == "--local-snapshot-path") {
            marker.local_snapshot_path = arg_value(args, i, arg);
        } else if (arg == "--final-snapshot-path") {
            marker.final_snapshot_path = arg_value(args, i, arg);
        } else if (arg == "--run-id") {
            marker.run_id = arg_value(args, i, arg);
        } else if (arg == "--timestamp") {
            marker.timestamp = arg_value(args, i, arg);
        } else {
            throw ValidationError("unknown state pending write option: " + arg);
        }
    }

    if (profile_state_dir.empty()) {
        throw ValidationError("state pending write requires --profile-state-dir");
    }
    btrfsbackup::write_pending_marker(profile_state_dir, marker);
}

void state_pending_read(const std::vector<std::string>& args, std::ostream& output) {
    fs::path marker_path;
    std::string field = "local_snapshot_path";

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--marker") {
            marker_path = arg_value(args, i, arg);
        } else if (arg == "--field") {
            field = arg_value(args, i, arg);
        } else {
            throw ValidationError("unknown state pending read option: " + arg);
        }
    }

    if (marker_path.empty()) {
        throw ValidationError("state pending read requires --marker");
    }
    output << btrfsbackup::read_pending_marker_field(marker_path, field) << '\n';
}

void state_pending_clear(const std::vector<std::string>& args) {
    fs::path marker_path;
    fs::path profile_state_dir;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--marker") {
            marker_path = arg_value(args, i, arg);
        } else if (arg == "--profile-state-dir") {
            profile_state_dir = arg_value(args, i, arg);
        } else {
            throw ValidationError("unknown state pending clear option: " + arg);
        }
    }

    if (marker_path.empty()) {
        throw ValidationError("state pending clear requires --marker");
    }
    if (profile_state_dir.empty()) {
        throw ValidationError("state pending clear requires --profile-state-dir");
    }
    btrfsbackup::clear_pending_marker(marker_path, profile_state_dir);
}

} // namespace btrfsbackup::command
