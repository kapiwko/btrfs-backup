// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cli/status/StatusCommand.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <core/Errors.hpp>
#include <cli/status/StatusHistoryCommand.hpp>
#include <cli/status/StatusShowCommand.hpp>
#include <core/Identifiers.hpp>
#include <platform/linux/filesystem/InotifyFileChangeWatcher.hpp>
#include <state/query/StatusService.hpp>

namespace fs = std::filesystem;

namespace {

[[noreturn]] void fail(const std::string& message, int code = 2) {
    std::cerr << "btrfs-backupctl status: " << message << '\n';
    std::exit(code);
}

struct WatchOptions {
    std::string profile = "default";
    std::optional<std::chrono::milliseconds> resync_interval;
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
            const std::chrono::duration<double> interval{
                std::stod(require_arg_value(args, i, arg))
            };
            if (interval <= std::chrono::duration<double>::zero()) {
                throw btrfsbackup::ValidationError("--interval must be greater than zero");
            }
            options.resync_interval = std::chrono::ceil<std::chrono::milliseconds>(interval);
        } else {
            throw btrfsbackup::ValidationError("unknown watch option: " + arg);
        }
    }
    btrfsbackup::validate_profile_id(options.profile);
    return options;
}

void watch(const fs::path& status_root, const std::vector<std::string>& args) {
    WatchOptions options = parse_watch_options(args);
    btrfsbackup::platform::linux::filesystem::InotifyFileChangeWatcher notifications(
        status_root / options.profile / "current.json"
    );
    std::string previous;
    (void)btrfsbackup::cli::status::status_watch_once(status_root, args, previous, std::cout);
    while (true) {
        notifications.wait_for_change(options.resync_interval);
        (void)btrfsbackup::cli::status::status_watch_once(status_root, args, previous, std::cout);
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

namespace btrfsbackup::cli::status {

bool status_watch_once(
    const fs::path& status_root,
    const std::vector<std::string>& args,
    std::string& previous,
    std::ostream& output
) {
    WatchOptions options = parse_watch_options(args);
    std::optional<btrfsbackup::state::StatusDocument> current = btrfsbackup::state::poll_status(status_root, options.profile, previous);
    if (!current)
        return false;
    output << current->content;
    if (current->content.empty() || current->content.back() != '\n') {
        output << '\n';
    }
    output.flush();
    previous = current->content;
    return true;
}

int status(const fs::path& status_root, const fs::path& history_root, const std::vector<std::string>& args) {
    if (args.empty()) {
        usage();
        return 2;
    }
    const std::string& command = args[0];
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

} // namespace btrfsbackup::cli::status
