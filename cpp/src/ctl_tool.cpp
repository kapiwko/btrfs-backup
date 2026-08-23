#include <btrfsbackup/ctl_tool.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <btrfsbackup/config_fingerprint.hpp>
#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/history.hpp>
#include <btrfsbackup/identifiers.hpp>
#include <btrfsbackup/installation_tool.hpp>
#include <btrfsbackup/profile_tool.hpp>
#include <btrfsbackup/run_state_command.hpp>
#include <btrfsbackup/status.hpp>
#include <btrfsbackup/status_write_command.hpp>

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
              << "  --profile-dir PATH   Override profile config dir (default: /etc/btrfs-backup/profiles.d).\n"
              << "\nCommands:\n"
              << "  profile COMMAND\n"
              << "  status COMMAND\n"
              << "  state COMMAND\n"
              << "  installation COMMAND\n"
              << "  -h, --help\n";
}

std::string read_text_file(const fs::path& path) {
    std::ifstream stream(path);
    if (!stream) {
        fail("cannot read " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

bool readable_file(const fs::path& path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec) && !ec && std::ifstream(path).good();
}

void command_watch(const fs::path& status_root, const std::vector<std::string>& args) {
    std::string profile = "default";
    double interval = 1.0;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--profile" && i + 1 < args.size()) {
            profile = args[++i];
        } else if (arg == "--interval" && i + 1 < args.size()) {
            interval = std::stod(args[++i]);
            if (interval <= 0) {
                fail("--interval must be greater than zero");
            }
        } else {
            fail("unknown watch option: " + arg);
        }
    }

    btrfsbackup::validate_profile_id(profile);
    fs::path path = status_root / profile / "current.json";
    std::string previous;
    while (true) {
        if (readable_file(path)) {
            std::string current = read_text_file(path);
            if (current != previous) {
                std::cout << current;
                if (current.empty() || current.back() != '\n') {
                    std::cout << '\n';
                }
                std::cout.flush();
                previous = std::move(current);
            }
        }
        std::this_thread::sleep_for(std::chrono::duration<double>(interval));
    }
}

void status_usage() {
    std::cout << "Usage: btrfs-backupctl status COMMAND\n"
              << "\nCommands:\n"
              << "  show [--profile ID|--all] [--human]\n"
              << "  history [--profile ID] [--limit N]\n"
              << "  watch [--profile ID] [--interval SECONDS]\n"
              << "  write [OPTIONS]\n";
}

int command_status_group(const fs::path& status_root, const fs::path& history_root, const std::vector<std::string>& args) {
    if (args.empty()) {
        status_usage();
        return 2;
    }
    std::string command = args[0];
    std::vector<std::string> rest(args.begin() + 1, args.end());
    if (command == "show") {
        btrfsbackup::command_status(status_root, history_root, rest, std::cout);
        return 0;
    }
    if (command == "history") {
        btrfsbackup::command_history(history_root, rest, std::cout);
        return 0;
    }
    if (command == "watch") {
        command_watch(status_root, rest);
        return 0;
    }
    if (command == "write") {
        btrfsbackup::command_write_status(status_root, history_root, rest);
        return 0;
    }
    if (command == "-h" || command == "--help") {
        status_usage();
        return 0;
    }
    fail("unknown status command: " + command);
}

void state_usage() {
    std::cout << "Usage: btrfs-backupctl state COMMAND\n"
              << "\nCommands:\n"
              << "  fingerprint [OPTIONS]\n"
              << "  check-last-success [OPTIONS]\n"
              << "  write-success [OPTIONS]\n"
              << "  migrate-legacy [OPTIONS]\n"
              << "  pending write|read|clear [OPTIONS]\n";
}

int command_pending_state(const std::vector<std::string>& args) {
    if (args.empty()) {
        state_usage();
        return 2;
    }
    std::string command = args[0];
    std::vector<std::string> rest(args.begin() + 1, args.end());
    if (command == "write") {
        btrfsbackup::command_write_pending_marker(rest);
        return 0;
    }
    if (command == "read") {
        btrfsbackup::command_read_pending_marker(rest, std::cout);
        return 0;
    }
    if (command == "clear") {
        btrfsbackup::command_clear_pending_marker(rest);
        return 0;
    }
    fail("unknown state pending command: " + command);
}

int command_state_group(const std::vector<std::string>& args) {
    if (args.empty()) {
        state_usage();
        return 2;
    }
    std::string command = args[0];
    std::vector<std::string> rest(args.begin() + 1, args.end());
    if (command == "fingerprint") {
        btrfsbackup::command_config_fingerprint(rest, std::cout);
        return 0;
    }
    if (command == "check-last-success") {
        btrfsbackup::command_check_last_success(rest, std::cout);
        return 0;
    }
    if (command == "write-success") {
        btrfsbackup::command_write_success_state(rest);
        return 0;
    }
    if (command == "migrate-legacy") {
        btrfsbackup::command_migrate_legacy_state(rest);
        return 0;
    }
    if (command == "pending") {
        return command_pending_state(rest);
    }
    if (command == "-h" || command == "--help") {
        state_usage();
        return 0;
    }
    fail("unknown state command: " + command);
}

} // namespace

namespace btrfsbackup {

int ctl_tool_main(int argc, char** argv) {
    fs::path status_root = std::getenv("BTRFS_BACKUP_STATUS_ROOT") ? std::getenv("BTRFS_BACKUP_STATUS_ROOT") : "/run/btrfs-backup/profiles";
    fs::path history_root = std::getenv("BTRFS_BACKUP_HISTORY_ROOT") ? std::getenv("BTRFS_BACKUP_HISTORY_ROOT") : "/var/lib/btrfs-backup/history";
    fs::path profile_config_dir = std::getenv("BTRFS_BACKUP_PROFILE_CONFIG_DIR") ? std::getenv("BTRFS_BACKUP_PROFILE_CONFIG_DIR") : "/etc/btrfs-backup/profiles.d";
    std::vector<std::string> rest;

    try {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--status-root") {
                status_root = arg_value(i, argc, argv, arg);
            } else if (arg == "--history-root") {
                history_root = arg_value(i, argc, argv, arg);
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
            return command_profile(args, profile_config_dir);
        } else if (command == "status") {
            return command_status_group(status_root, history_root, args);
        } else if (command == "state") {
            return command_state_group(args);
        } else if (command == "installation") {
            return command_installation(args);
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
