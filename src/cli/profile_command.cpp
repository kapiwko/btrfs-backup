// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <cli/profile_create_command.hpp>
#include <cli/profile_command.hpp>
#include <cli/profile_wizard_command.hpp>
#include <core/errors.hpp>
#include <config/application_config.hpp>
#include <config/json_io.hpp>
#include <config/profile.hpp>
#include <cli/profile_list_command.hpp>
#include <config/profile_service.hpp>

namespace fs = std::filesystem;
using btrfsbackup::ValidationError;
using btrfsbackup::dump_json;
using btrfsbackup::Profile;
using btrfsbackup::profile_to_json;

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
    std::cout << "Usage: btrfs-backupctl profile [--etc-root PATH] [--udev-root PATH] [--systemd-root PATH] [--public-root PATH] COMMAND\n"
              << "\nCommands:\n"
              << "  list\n"
              << "  wizard [OPTIONS]\n"
              << "  create --output PATH [OPTIONS]\n"
              << "  validate --file PATH\n"
              << "  render --file PATH --output-dir PATH\n"
              << "  save --file PATH\n"
              << "  show [--profile ID]\n"
              << "  export [--profile ID] --output PATH\n";
}

} // namespace

namespace btrfsbackup::command {

int profile(
    const std::vector<std::string>& args,
    const fs::path& profile_config_dir,
    IConfigurationActivator& system_activator
) {
    fs::path etc_root = std::getenv("BTRFS_BACKUP_ETC_ROOT") ? std::getenv("BTRFS_BACKUP_ETC_ROOT") : "/etc/btrfs-backup";
    fs::path udev_root = std::getenv("BTRFS_BACKUP_UDEV_ROOT") ? std::getenv("BTRFS_BACKUP_UDEV_ROOT") : "/etc/udev/rules.d";
    fs::path systemd_root = std::getenv("BTRFS_BACKUP_SYSTEMD_ROOT") ? std::getenv("BTRFS_BACKUP_SYSTEMD_ROOT") : "/etc/systemd/system";
    fs::path public_root = std::getenv("BTRFS_BACKUP_PUBLIC_ROOT") ? std::getenv("BTRFS_BACKUP_PUBLIC_ROOT") : "/var/lib/btrfs-backup/public/profiles";
    std::vector<std::string> rest;

    try {
        for (std::size_t i = 0; i < args.size(); ++i) {
            const std::string& arg = args[i];
            if (arg == "--etc-root") {
                etc_root = arg_value(i, args, arg);
            } else if (arg == "--udev-root") {
                udev_root = arg_value(i, args, arg);
            } else if (arg == "--systemd-root") {
                systemd_root = arg_value(i, args, arg);
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
        if (command == "wizard") {
            return profile_wizard(std::vector<std::string>(rest.begin() + 1, rest.end()));
        }
        if (command != "validate" && command != "render" && command != "save"
            && command != "show" && command != "export") {
            fail("unknown command: " + command);
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

        if (command == "validate") {
            if (file.empty()) fail("validate requires --file");
            ApplicationConfig config = ApplicationConfig::load(etc_root);
            std::cout << dump_json(profile_to_json(validate_profile_file(file, config.paths().target_mount_root)));
        } else if (command == "render") {
            if (file.empty()) fail("render requires --file");
            if (output_dir.empty()) fail("render requires --output-dir");
            ApplicationConfig config = ApplicationConfig::load(etc_root);
            Profile profile = validate_profile_file(file, config.paths().target_mount_root);
            render_profile(file, output_dir, config.paths().target_mount_root);
            std::cout << "Rendered profile " << profile.id.value() << " to " << output_dir << "\n";
        } else if (command == "save") {
            if (file.empty()) fail("save requires --file");
            if (geteuid() != 0 && etc_root == "/etc/btrfs-backup") {
                fail("save to system configuration must be run as root", 1);
            }
            const bool installs_system_configuration = fs::absolute(etc_root).lexically_normal() == fs::path("/etc/btrfs-backup") && fs::absolute(udev_root).lexically_normal() == fs::path("/etc/udev/rules.d") && fs::absolute(systemd_root).lexically_normal() == fs::path("/etc/systemd/system") && fs::absolute(public_root).lexically_normal() == fs::path("/var/lib/btrfs-backup/public/profiles");
            NullConfigurationActivator null_activator;
            IConfigurationActivator& activator = installs_system_configuration
                ? system_activator
                : static_cast<IConfigurationActivator&>(null_activator);
            Profile profile = save_profile(file, {etc_root, udev_root, systemd_root, public_root}, activator);
            std::cout << "Saved profile " << profile.id.value() << "\n";
        } else if (command == "show") {
            std::cout << dump_json(profile_to_json(get_profile(etc_root, profile_id)));
        } else if (command == "export") {
            if (output_dir.empty()) fail("export requires --output");
            Profile profile = export_profile(etc_root, profile_id, output_dir);
            std::cout << "Exported profile " << profile.id.value() << " to " << output_dir << "\n";
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
