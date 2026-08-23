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
#include <btrfsbackup/profile_list.hpp>
#include <btrfsbackup/run_state_command.hpp>
#include <btrfsbackup/source_definition.hpp>
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
              << "  --legacy-config PATH Override legacy config path (default: /etc/btrfs-backup/backup.env).\n"
              << "\nCommands:\n"
              << "  list-profiles\n"
              << "  status [--profile ID|--all] [--human]\n"
              << "  history [--profile ID] [--limit N]\n"
              << "  watch [--profile ID] [--interval SECONDS]\n"
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

} // namespace

namespace btrfsbackup {

int ctl_tool_main(int argc, char** argv) {
    fs::path status_root = std::getenv("BTRFS_BACKUP_STATUS_ROOT") ? std::getenv("BTRFS_BACKUP_STATUS_ROOT") : "/run/btrfs-backup/profiles";
    fs::path history_root = std::getenv("BTRFS_BACKUP_HISTORY_ROOT") ? std::getenv("BTRFS_BACKUP_HISTORY_ROOT") : "/var/lib/btrfs-backup/history";
    fs::path profile_config_dir = std::getenv("BTRFS_BACKUP_PROFILE_CONFIG_DIR") ? std::getenv("BTRFS_BACKUP_PROFILE_CONFIG_DIR") : "/etc/btrfs-backup/profiles.d";
    fs::path legacy_config_file = std::getenv("BTRFS_BACKUP_LEGACY_CONFIG") ? std::getenv("BTRFS_BACKUP_LEGACY_CONFIG") : "/etc/btrfs-backup/backup.env";
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
            } else if (arg == "--legacy-config") {
                legacy_config_file = arg_value(i, argc, argv, arg);
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

        if (command == "list-profiles") {
            command_list_profiles(profile_config_dir, legacy_config_file, std::cout);
        } else if (command == "status") {
            command_status(status_root, history_root, args, std::cout);
        } else if (command == "history") {
            command_history(history_root, args, std::cout);
        } else if (command == "watch") {
            command_watch(status_root, args);
        } else if (command == "write-status") {
            command_write_status(status_root, history_root, args);
        } else if (command == "config-fingerprint") {
            command_config_fingerprint(args, std::cout);
        } else if (command == "check-last-success") {
            command_check_last_success(args, std::cout);
        } else if (command == "write-success-state") {
            command_write_success_state(args);
        } else if (command == "migrate-legacy-state") {
            command_migrate_legacy_state(args);
        } else if (command == "write-pending-marker") {
            command_write_pending_marker(args);
        } else if (command == "read-pending-marker") {
            command_read_pending_marker(args, std::cout);
        } else if (command == "clear-pending-marker") {
            command_clear_pending_marker(args);
        } else if (command == "parse-source-definition") {
            command_parse_source_definition(args, std::cout);
        } else if (command == "parse-profile-sources") {
            command_parse_profile_sources(args, std::cout);
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
