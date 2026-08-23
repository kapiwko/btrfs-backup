#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <btrfsbackup/command/profile_create_command.hpp>
#include <btrfsbackup/command/profile_command.hpp>
#include <btrfsbackup/command/profile_migrate_command.hpp>
#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/file_io.hpp>
#include <btrfsbackup/json_io.hpp>
#include <btrfsbackup/profile.hpp>
#include <btrfsbackup/command/profile_list_command.hpp>
#include <btrfsbackup/profile_render.hpp>
#include <btrfsbackup/profile_store.hpp>
#include <btrfsbackup/command/profile_sources_command.hpp>

namespace fs = std::filesystem;
using btrfsbackup::ValidationError;
using btrfsbackup::atomic_write;
using btrfsbackup::dump_json;
using btrfsbackup::load_json_file;
using btrfsbackup::load_profile_by_id;
using btrfsbackup::Profile;
using btrfsbackup::profile_from_json;
using btrfsbackup::profile_to_json;
using btrfsbackup::render_profile_env;
using btrfsbackup::render_tree;
using btrfsbackup::save_tree;

namespace {

[[noreturn]] void fail(const std::string& message, int code = 2) {
    std::cerr << "btrfs-backupctl profile: " << message << '\n';
    std::exit(code);
}

std::string arg_value(std::size_t& index, const std::vector<std::string>& args, const std::string& option) {
    if (index + 1 >= args.size()) {
        fail(option + " requires a value");
    }
    return args[++index];
}

void usage() {
    std::cout << "Usage: btrfs-backupctl profile [--etc-root PATH] [--udev-root PATH] [--public-root PATH] COMMAND\n"
              << "\nCommands:\n"
              << "  list\n"
              << "  migrate [OPTIONS]\n"
              << "  sources --file PATH\n"
              << "  create --output PATH [OPTIONS]\n"
              << "  validate --file PATH\n"
              << "  render --file PATH --output-dir PATH\n"
              << "  save --file PATH\n"
              << "  show [--profile ID]\n"
              << "  export [--profile ID] --output PATH\n";
}

} // namespace

namespace btrfsbackup::command {

int profile(const std::vector<std::string>& args, const fs::path& profile_config_dir) {
    fs::path etc_root = std::getenv("BTRFS_BACKUP_ETC_ROOT") ? std::getenv("BTRFS_BACKUP_ETC_ROOT") : "/etc/btrfs-backup";
    fs::path udev_root = std::getenv("BTRFS_BACKUP_UDEV_ROOT") ? std::getenv("BTRFS_BACKUP_UDEV_ROOT") : "/etc/udev/rules.d";
    fs::path public_root = std::getenv("BTRFS_BACKUP_PUBLIC_ROOT") ? std::getenv("BTRFS_BACKUP_PUBLIC_ROOT") : "/var/lib/btrfs-backup/public/profiles";
    std::vector<std::string> rest;

    try {
        for (std::size_t i = 0; i < args.size(); ++i) {
            const std::string& arg = args[i];
            if (arg == "--etc-root") {
                etc_root = arg_value(i, args, arg);
            } else if (arg == "--udev-root") {
                udev_root = arg_value(i, args, arg);
            } else if (arg == "--public-root") {
                public_root = arg_value(i, args, arg);
            } else if (arg == "-h" || arg == "--help") {
                usage();
                return 0;
            } else {
                for (; i < args.size(); ++i) {
                    rest.emplace_back(args[i]);
                }
                break;
            }
        }
        if (rest.empty()) {
            usage();
            return 2;
        }
        std::string command = rest[0];
        if (command == "create") {
            return profile_create(std::vector<std::string>(rest.begin() + 1, rest.end()));
        }
        if (command == "list") {
            profile_list(profile_config_dir, profile_config_dir.parent_path() / "profiles", std::cout);
            return 0;
        }
        if (command == "migrate") {
            return profile_migrate(std::vector<std::string>(rest.begin() + 1, rest.end()));
        }
        if (command == "sources") {
            profile_sources(std::vector<std::string>(rest.begin() + 1, rest.end()), std::cout);
            return 0;
        }
        fs::path file;
        fs::path output_dir;
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
            } else if (arg == "-h" || arg == "--help") {
                usage();
                return 0;
            } else {
                fail("unknown option: " + arg);
            }
        }

        if (command == "emit-runtime-env") {
            if (file.empty()) fail("emit-runtime-env requires --file");
            std::cout << render_profile_env(profile_from_json(load_json_file(file)));
        } else if (command == "validate") {
            if (file.empty()) fail("validate requires --file");
            std::cout << dump_json(profile_to_json(profile_from_json(load_json_file(file))));
        } else if (command == "render") {
            if (file.empty()) fail("render requires --file");
            if (output_dir.empty()) fail("render requires --output-dir");
            output_dir = fs::absolute(output_dir).lexically_normal();
            if (output_dir == "/" || output_dir == "/etc" || output_dir == "/usr" || output_dir == "/var") {
                throw ValidationError("refusing unsafe output directory: " + output_dir.string());
            }
            std::error_code ec;
            fs::remove_all(output_dir, ec);
            Profile profile = profile_from_json(load_json_file(file));
            render_tree(profile, output_dir);
            std::cout << "Rendered profile " << profile.id << " to " << output_dir << "\n";
        } else if (command == "save") {
            if (file.empty()) fail("save requires --file");
            if (geteuid() != 0 && etc_root == "/etc/btrfs-backup") {
                fail("save to system configuration must be run as root", 1);
            }
            Profile profile = profile_from_json(load_json_file(file));
            save_tree(profile, etc_root, udev_root, public_root);
            std::cout << "Saved profile " << profile.id << "\n";
        } else if (command == "show") {
            std::cout << dump_json(profile_to_json(load_profile_by_id(etc_root, profile_id)));
        } else if (command == "export") {
            if (output_dir.empty()) fail("export requires --output");
            Profile profile = load_profile_by_id(etc_root, profile_id);
            atomic_write(output_dir, dump_json(profile_to_json(profile)), 0600);
            std::cout << "Exported profile " << profile.id << " to " << output_dir << "\n";
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

} // namespace btrfsbackup::command
