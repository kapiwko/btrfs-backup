#include <btrfsbackup/command/status_write_command.hpp>

#include <filesystem>
#include <regex>
#include <string>
#include <vector>

#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/status_writer.hpp>

namespace fs = std::filesystem;

namespace {

std::string arg_value(const std::vector<std::string>& args, std::size_t& index, const std::string& option) {
    if (index + 1 >= args.size()) {
        throw btrfsbackup::ValidationError(option + " requires a value");
    }
    return args[++index];
}

int parse_int(const std::string& option, const std::string& value) {
    static const std::regex pattern("^[0-9]+$");
    if (!std::regex_match(value, pattern)) {
        throw btrfsbackup::ValidationError(option + " must be a number");
    }
    return std::stoi(value);
}

} // namespace

namespace btrfsbackup::command {

void status_write(
    const fs::path& status_root,
    const fs::path& history_root,
    const std::vector<std::string>& args
) {
    btrfsbackup::StatusRecord record;
    bool current = false;
    bool history = false;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--current") {
            current = true;
        } else if (arg == "--history") {
            history = true;
        } else if (arg == "--profile-id") {
            record.profile_id = arg_value(args, i, arg);
        } else if (arg == "--profile-name") {
            record.profile_name = arg_value(args, i, arg);
        } else if (arg == "--run-id") {
            record.run_id = arg_value(args, i, arg);
        } else if (arg == "--state") {
            record.state = arg_value(args, i, arg);
        } else if (arg == "--phase") {
            record.phase = arg_value(args, i, arg);
        } else if (arg == "--message") {
            record.message = arg_value(args, i, arg);
        } else if (arg == "--current-source-name") {
            record.current_source_name = arg_value(args, i, arg);
        } else if (arg == "--source-index") {
            record.source_index = parse_int(arg, arg_value(args, i, arg));
        } else if (arg == "--source-count") {
            record.source_count = parse_int(arg, arg_value(args, i, arg));
        } else if (arg == "--started-at") {
            record.started_at = arg_value(args, i, arg);
        } else if (arg == "--updated-at") {
            record.updated_at = arg_value(args, i, arg);
        } else if (arg == "--finished-at") {
            record.finished_at = arg_value(args, i, arg);
        } else if (arg == "--error") {
            record.error = arg_value(args, i, arg);
        } else if (arg == "--exit-code") {
            record.exit_code = parse_int(arg, arg_value(args, i, arg));
        } else {
            throw ValidationError("unknown status write option: " + arg);
        }
    }

    if (!current && !history) {
        throw ValidationError("status write requires --current or --history");
    }
    if (history && record.finished_at.empty()) {
        throw ValidationError("--history requires --finished-at");
    }

    if (current) {
        btrfsbackup::write_current_status(status_root, record);
    }
    if (history) {
        btrfsbackup::write_history_entry(history_root, record);
    }
}

} // namespace btrfsbackup::command
