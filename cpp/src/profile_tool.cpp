#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <btrfsbackup/profile_tool.hpp>
#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/file_io.hpp>
#include <btrfsbackup/json.hpp>
#include <btrfsbackup/json_io.hpp>
#include <btrfsbackup/profile.hpp>
#include <btrfsbackup/profile_compose.hpp>
#include <btrfsbackup/profile_store.hpp>

namespace fs = std::filesystem;
using json = btrfsbackup::Json;
using btrfsbackup::ValidationError;
using btrfsbackup::atomic_write;
using btrfsbackup::dump_json;
using btrfsbackup::load_json_file;
using btrfsbackup::load_profile_by_id;
using btrfsbackup::normalize_profile;
using btrfsbackup::profile_from_environment_sources;
using btrfsbackup::render_tree;
using btrfsbackup::save_tree;

namespace {

[[noreturn]] void fail(const std::string& message, int code = 2) {
    std::cerr << "btrfs-backup-profile: " << message << '\n';
    std::exit(code);
}

std::string arg_value(int& index, int argc, char** argv, const std::string& option) {
    if (index + 1 >= argc) {
        fail(option + " requires a value");
    }
    return argv[++index];
}

void usage() {
    std::cout << "Usage: btrfs-backup-profile [--etc-root PATH] [--udev-root PATH] [--public-root PATH] COMMAND\n"
              << "\nCommands:\n"
              << "  compose --sources-table PATH --output PATH\n"
              << "  validate --file PATH\n"
              << "  render --file PATH --output-dir PATH\n"
              << "  save --file PATH\n"
              << "  show [--profile ID]\n"
              << "  export [--profile ID] --output PATH\n";
}

} // namespace

namespace btrfsbackup {

int profile_tool_main(int argc, char** argv) {
    fs::path etc_root = std::getenv("BTRFS_BACKUP_ETC_ROOT") ? std::getenv("BTRFS_BACKUP_ETC_ROOT") : "/etc/btrfs-backup";
    fs::path udev_root = std::getenv("BTRFS_BACKUP_UDEV_ROOT") ? std::getenv("BTRFS_BACKUP_UDEV_ROOT") : "/etc/udev/rules.d";
    fs::path public_root = std::getenv("BTRFS_BACKUP_PUBLIC_ROOT") ? std::getenv("BTRFS_BACKUP_PUBLIC_ROOT") : "/var/lib/btrfs-backup/public/profiles";
    std::vector<std::string> rest;

    try {
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--etc-root") {
                etc_root = arg_value(i, argc, argv, arg);
            } else if (arg == "--udev-root") {
                udev_root = arg_value(i, argc, argv, arg);
            } else if (arg == "--public-root") {
                public_root = arg_value(i, argc, argv, arg);
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
        fs::path file;
        fs::path output_dir;
        fs::path sources_table;
        std::string profile_id = "default";
        for (std::size_t i = 1; i < rest.size(); ++i) {
            const std::string& arg = rest[i];
            if (arg == "--file" && i + 1 < rest.size()) {
                file = rest[++i];
            } else if (arg == "--output-dir" && i + 1 < rest.size()) {
                output_dir = rest[++i];
            } else if (arg == "--profile" && i + 1 < rest.size()) {
                profile_id = rest[++i];
            } else if (arg == "--output" && i + 1 < rest.size()) {
                output_dir = rest[++i];
            } else if (arg == "--sources-table" && i + 1 < rest.size()) {
                sources_table = rest[++i];
            } else if (arg == "-h" || arg == "--help") {
                usage();
                return 0;
            } else {
                fail("unknown option: " + arg);
            }
        }

        if (command == "compose") {
            if (sources_table.empty()) fail("compose requires --sources-table");
            if (output_dir.empty()) fail("compose requires --output");
            atomic_write(output_dir, dump_json(profile_from_environment_sources(sources_table)), 0600);
        } else if (command == "validate") {
            if (file.empty()) fail("validate requires --file");
            std::cout << dump_json(normalize_profile(load_json_file(file)));
        } else if (command == "render") {
            if (file.empty()) fail("render requires --file");
            if (output_dir.empty()) fail("render requires --output-dir");
            output_dir = fs::absolute(output_dir).lexically_normal();
            if (output_dir == "/" || output_dir == "/etc" || output_dir == "/usr" || output_dir == "/var") {
                throw ValidationError("refusing unsafe output directory: " + output_dir.string());
            }
            std::error_code ec;
            fs::remove_all(output_dir, ec);
            json profile = normalize_profile(load_json_file(file));
            render_tree(profile, output_dir);
            std::cout << "Rendered profile " << profile.at("profileId").get<std::string>() << " to " << output_dir << "\n";
        } else if (command == "save") {
            if (file.empty()) fail("save requires --file");
            if (geteuid() != 0 && etc_root == "/etc/btrfs-backup") {
                fail("save to system configuration must be run as root", 1);
            }
            json profile = normalize_profile(load_json_file(file));
            save_tree(profile, etc_root, udev_root, public_root);
            std::cout << "Saved profile " << profile.at("profileId").get<std::string>() << "\n";
        } else if (command == "show") {
            std::cout << dump_json(load_profile_by_id(etc_root, profile_id));
        } else if (command == "export") {
            if (output_dir.empty()) fail("export requires --output");
            json profile = load_profile_by_id(etc_root, profile_id);
            atomic_write(output_dir, dump_json(profile), 0600);
            std::cout << "Exported profile " << profile.at("profileId").get<std::string>() << " to " << output_dir << "\n";
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
