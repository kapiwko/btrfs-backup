#include <btrfsbackup/ctl_tool.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/json.hpp>
#include <btrfsbackup/json_io.hpp>

namespace fs = std::filesystem;
using json = btrfsbackup::Json;

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

void validate_profile_id(const std::string& profile_id) {
    static const std::regex pattern("^[A-Za-z0-9][A-Za-z0-9._-]*$");
    if (!std::regex_match(profile_id, pattern)) {
        fail("invalid profile id: " + profile_id);
    }
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

std::string string_or_empty(const json& data, const char* key) {
    auto it = data.find(key);
    if (it == data.end() || !it->is_string()) {
        return {};
    }
    return it->get<std::string>();
}

void print_json_file(const fs::path& path) {
    std::string content = read_text_file(path);
    std::cout << content;
    if (content.empty() || content.back() != '\n') {
        std::cout << '\n';
    }
}

void print_human_status(const fs::path& path) {
    json data = btrfsbackup::load_json_file(path);
    std::string profile = string_or_empty(data, "profileName");
    if (profile.empty()) {
        profile = string_or_empty(data, "profileId");
    }
    std::string state = string_or_empty(data, "state");
    std::cout << (profile.empty() ? "unknown" : profile) << ": " << (state.empty() ? "unknown" : state) << '\n';

    const std::vector<std::pair<const char*, const char*>> fields = {
        {"phase", "  phase: "},
        {"message", "  "},
        {"currentSourceName", "  source: "},
        {"updatedAt", "  updated: "},
        {"error", "  error: "},
    };
    for (const auto& [key, prefix] : fields) {
        std::string value = string_or_empty(data, key);
        if (!value.empty()) {
            std::cout << prefix << value << '\n';
        }
    }
}

std::vector<fs::path> sorted_files(const fs::path& directory, const std::string& suffix, bool reverse = false) {
    std::vector<fs::path> files;
    std::error_code ec;
    if (!fs::is_directory(directory, ec) || ec) {
        return files;
    }
    for (const auto& entry : fs::directory_iterator(directory, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_regular_file(ec) || ec) {
            ec.clear();
            continue;
        }
        std::string name = entry.path().filename().string();
        if (name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    if (reverse) {
        std::reverse(files.begin(), files.end());
    }
    return files;
}

void command_status(const fs::path& status_root, const fs::path& history_root, const std::vector<std::string>& args) {
    std::string profile = "default";
    bool all = false;
    bool human = false;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--profile" && i + 1 < args.size()) {
            profile = args[++i];
        } else if (arg == "--all") {
            all = true;
        } else if (arg == "--human") {
            human = true;
        } else {
            fail("unknown status option: " + arg);
        }
    }

    if (all) {
        std::vector<fs::path> files;
        std::error_code ec;
        if (fs::is_directory(status_root, ec) && !ec) {
            for (const auto& profile_entry : fs::directory_iterator(status_root, ec)) {
                if (ec) {
                    break;
                }
                fs::path current = profile_entry.path() / "current.json";
                if (readable_file(current)) {
                    files.push_back(current);
                }
            }
        }
        std::sort(files.begin(), files.end());
        if (files.empty()) {
            fail("no status files found under " + status_root.string());
        }
        for (const auto& path : files) {
            if (human) {
                print_human_status(path);
            } else {
                print_json_file(path);
            }
        }
        return;
    }

    validate_profile_id(profile);
    fs::path path = status_root / profile / "current.json";
    if (!readable_file(path) && readable_file(history_root / profile / "last.json")) {
        path = history_root / profile / "last.json";
    }
    if (human) {
        if (!readable_file(path)) {
            fail("cannot read " + path.string());
        }
        print_human_status(path);
    } else {
        print_json_file(path);
    }
}

void command_history(const fs::path& history_root, const std::vector<std::string>& args) {
    std::string profile = "default";
    int limit = 50;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--profile" && i + 1 < args.size()) {
            profile = args[++i];
        } else if (arg == "--limit" && i + 1 < args.size()) {
            std::string value = args[++i];
            if (!std::regex_match(value, std::regex("^[0-9]+$"))) {
                fail("--limit must be a number");
            }
            limit = std::stoi(value);
        } else {
            fail("unknown history option: " + arg);
        }
    }

    validate_profile_id(profile);
    fs::path directory = history_root / profile;
    std::error_code ec;
    if (!fs::is_directory(directory, ec) || ec) {
        std::cout << "[]\n";
        return;
    }

    std::vector<fs::path> files;
    for (const auto& path : sorted_files(directory, ".json", true)) {
        if (path.filename() != "last.json") {
            files.push_back(path);
        }
    }

    std::cout << "[\n";
    int count = 0;
    for (const auto& path : files) {
        if (count >= limit) {
            break;
        }
        if (count > 0) {
            std::cout << ",\n";
        }
        std::cout << read_text_file(path);
        ++count;
    }
    std::cout << "\n]\n";
}

void command_list_profiles(const fs::path& profile_config_dir, const fs::path& legacy_config_file) {
    bool found = false;
    std::vector<fs::path> files = sorted_files(profile_config_dir, ".env");
    for (const auto& file : files) {
        std::string profile = file.stem().string();
        validate_profile_id(profile);
        std::cout << profile << '\n';
        found = true;
    }

    std::error_code ec;
    bool has_legacy = fs::is_regular_file(legacy_config_file, ec) && !ec;
    ec.clear();
    bool has_default_profile = fs::exists(profile_config_dir / "default.env", ec) && !ec;
    if (has_legacy && !has_default_profile) {
        std::cout << "default (legacy)\n";
        found = true;
    }
    if (!found) {
        fail("no profiles found");
    }
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

    validate_profile_id(profile);
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
            command_list_profiles(profile_config_dir, legacy_config_file);
        } else if (command == "status") {
            command_status(status_root, history_root, args);
        } else if (command == "history") {
            command_history(history_root, args);
        } else if (command == "watch") {
            command_watch(status_root, args);
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
