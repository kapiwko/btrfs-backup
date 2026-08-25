// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cli/ctl_tool.hpp>

#include <algorithm>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <config/application_config.hpp>
#include <cli/backup_tool.hpp>
#include <cli/installation_command.hpp>
#include <cli/profile_command.hpp>
#include <cli/runner_command.hpp>
#include <cli/status_command.hpp>
#include <cli/target_command.hpp>
#include <config/errors.hpp>
#include <backup/transfer_pipeline.hpp>

namespace fs = std::filesystem;

namespace {

[[noreturn]] void fail(const std::string& message, int code = 2) {
    std::cerr << "btrfs-backupctl: " << message << '\n';
    std::exit(code);
}

std::string arg_value(int& index, int argc, char** argv, const std::string& option) {
    if (index + 1 >= argc) {
        fail(option + " requires a value");
    }
    return argv[++index];
}

void usage() {
    std::cout << "Usage: btrfs-backupctl [options] COMMAND\n"
              << "\nOptions:\n"
              << "  --status-root PATH   Override status root (default: /run/btrfs-backup/profiles).\n"
              << "  --history-root PATH  Override history root (default: /var/lib/btrfs-backup/history).\n"
              << "  --profile-dir PATH   Override profile config root (default: /etc/btrfs-backup).\n"
              << "\nCommands:\n"
              << "  profile COMMAND\n"
              << "  status COMMAND\n"
              << "  installation COMMAND\n"
              << "  runner COMMAND\n"
              << "  target COMMAND\n"
              << "  -h, --help\n";
}

} // namespace

namespace btrfsbackup {

int ctl_tool_main(int argc, char** argv) {
    bool status_root_overridden = std::getenv("BTRFS_BACKUP_STATUS_ROOT") != nullptr;
    bool history_root_overridden = std::getenv("BTRFS_BACKUP_HISTORY_ROOT") != nullptr;
    fs::path status_root = status_root_overridden ? std::getenv("BTRFS_BACKUP_STATUS_ROOT") : "/run/btrfs-backup/profiles";
    fs::path history_root = history_root_overridden ? std::getenv("BTRFS_BACKUP_HISTORY_ROOT") : "/var/lib/btrfs-backup/history";
    fs::path profile_config_dir = std::getenv("BTRFS_BACKUP_PROFILE_CONFIG_DIR") ? std::getenv("BTRFS_BACKUP_PROFILE_CONFIG_DIR") : "/etc/btrfs-backup";
    std::vector<std::string> rest;

    try {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--status-root") {
                status_root = arg_value(i, argc, argv, arg);
                status_root_overridden = true;
            } else if (arg == "--history-root") {
                history_root = arg_value(i, argc, argv, arg);
                history_root_overridden = true;
            } else if (arg == "--profile-dir") {
                profile_config_dir = arg_value(i, argc, argv, arg);
            } else if (arg == "-h" || arg == "--help") {
                usage();
                return 0;
            } else {
                for (; i < argc; ++i) {
                    rest.emplace_back(argv[i]);
                }
                break;
            }
        }

        if (rest.empty()) {
            usage();
            return 2;
        }
        std::string command = rest[0];
        std::vector<std::string> args(rest.begin() + 1, rest.end());

        if (command == "profile") {
            return command::profile(args, profile_config_dir);
        } else if (command == "status") {
            bool help_requested = std::find(args.begin(), args.end(), "-h") != args.end()
                || std::find(args.begin(), args.end(), "--help") != args.end();
            if (!help_requested) {
                ApplicationConfig application_config = ApplicationConfig::load(profile_config_dir);
                if (!status_root_overridden) status_root = application_config.paths().status_root;
                if (!history_root_overridden) history_root = application_config.paths().history_root;
            }
            return command::status(status_root, history_root, args);
        } else if (command == "installation") {
            return command::installation(args);
        } else if (command == "runner") {
            CancellationToken cancellation;
            TerminationSignalMonitor termination_signals(cancellation);
            return command::runner(profile_config_dir, args, std::cout, cancellation);
        } else if (command == "target") {
            return command::target(profile_config_dir, args, std::cout);
        } else if (command == "-h" || command == "--help") {
            usage();
        } else {
            fail("unknown command: " + command);
        }
    } catch (const ValidationError& exc) {
        fail(exc.what());
    } catch (const std::exception& exc) {
        fail(exc.what());
    }
    return 0;
}

} // namespace btrfsbackup
