#include <btrfsbackup/command/status_command.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/command/status_history_command.hpp>
#include <btrfsbackup/command/status_show_command.hpp>
#include <btrfsbackup/identifiers.hpp>
#include <btrfsbackup/json.hpp>

namespace fs = std::filesystem;

namespace {

[[noreturn]] void fail(const std::string& message, int code = 2) {
    std::cerr << "btrfs-backupctl status: " << message << '\n';
    std::exit(code);
}

std::string read_text_file(const fs::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        throw btrfsbackup::ValidationError("cannot read " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

bool readable_file(const fs::path& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec) && !ec && std::ifstream(path).good();
}

struct WatchOptions {
    std::string profile = "default";
    double interval = 1.0;
};

std::string require_arg_value(const std::vector<std::string>& args, std::size_t& index, const std::string& option) {
    if (index + 1 >= args.size()) {
        throw btrfsbackup::ValidationError(option + " requires a value");
    }
    return args[++index];
}

WatchOptions parse_watch_options(const std::vector<std::string>& args) {
    WatchOptions options;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--profile") {
            options.profile = require_arg_value(args, i, arg);
        } else if (arg == "--interval") {
            options.interval = std::stod(require_arg_value(args, i, arg));
            if (options.interval <= 0) {
                throw btrfsbackup::ValidationError("--interval must be greater than zero");
            }
        } else {
            throw btrfsbackup::ValidationError("unknown watch option: " + arg);
        }
    }
    btrfsbackup::validate_profile_id(options.profile);
    return options;
}

void validate_status_api_json(const std::string& content) {
    btrfsbackup::Json data = btrfsbackup::Json::parse(content);
    if (!data.is_object()) {
        throw btrfsbackup::ValidationError("status JSON must be an object");
    }
    if (!data.contains("schemaVersion") || data.at("schemaVersion") != 2) {
        throw btrfsbackup::ValidationError("status JSON has unsupported schemaVersion");
    }
    const std::vector<std::string> required_fields = {
        "profileId",
        "profileName",
        "runId",
        "state",
        "phase",
        "message",
        "currentSourceName",
        "sourceIndex",
        "sourceCount",
        "startedAt",
        "updatedAt",
        "finishedAt",
        "errorCode",
        "errorMessage",
        "details",
        "recoverable",
        "suggestedAction",
        "canCancel",
        "bytesProcessed",
        "bytesTotalEstimated",
        "runBytesProcessed",
        "speedBps",
        "etaSeconds",
        "sourceProgress",
        "overallProgress",
        "progressAccuracy",
        "exitCode",
    };
    for (const std::string& field : required_fields) {
        if (!data.contains(field)) {
            throw btrfsbackup::ValidationError("status JSON is missing required field: " + field);
        }
    }
}

void watch(const fs::path& status_root, const std::vector<std::string>& args) {
    WatchOptions options = parse_watch_options(args);
    std::string previous;
    while (true) {
        (void)btrfsbackup::command::status_watch_once(status_root, args, previous, std::cout);
        std::this_thread::sleep_for(std::chrono::duration<double>(options.interval));
    }
}

void usage() {
    std::cout << "Usage: btrfs-backupctl status COMMAND\n"
              << "\nCommands:\n"
              << "  show [--profile ID|--all] [--human]\n"
              << "  history [--profile ID] [--limit N]\n"
              << "  watch [--profile ID] [--interval SECONDS]\n";
}

} // namespace

namespace btrfsbackup::command {

bool status_watch_once(
    const fs::path& status_root,
    const std::vector<std::string>& args,
    std::string& previous,
    std::ostream& output
) {
    WatchOptions options = parse_watch_options(args);
    fs::path path = status_root / options.profile / "current.json";
    if (!readable_file(path)) {
        return false;
    }

    std::string current = read_text_file(path);
    if (current == previous) {
        return false;
    }
    validate_status_api_json(current);
    output << current;
    if (current.empty() || current.back() != '\n') {
        output << '\n';
    }
    output.flush();
    previous = std::move(current);
    return true;
}

int status(const fs::path& status_root, const fs::path& history_root, const std::vector<std::string>& args) {
    if (args.empty()) {
        usage();
        return 2;
    }
    std::string command = args[0];
    std::vector<std::string> rest(args.begin() + 1, args.end());
    if (command == "show") {
        status_show(status_root, history_root, rest, std::cout);
        return 0;
    }
    if (command == "history") {
        status_history(history_root, rest, std::cout);
        return 0;
    }
    if (command == "watch") {
        watch(status_root, rest);
        return 0;
    }
    if (command == "-h" || command == "--help") {
        usage();
        return 0;
    }
    fail("unknown command: " + command);
}

} // namespace btrfsbackup::command
