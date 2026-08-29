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
#include <platform/linux/config/application_config.hpp>
#include <platform/linux/config/profile_activation_migration.hpp>
#include <config/model/json_io.hpp>
#include <config/model/profile.hpp>
#include <config/model/profile_document.hpp>
#include <cli/profile_list_command.hpp>
#include <platform/linux/config/profile_service.hpp>

namespace fs = std::filesystem;
using btrfsbackup::ValidationError;
using btrfsbackup::config::dump_json;
using btrfsbackup::config::Profile;
using btrfsbackup::config::profile_to_json;

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
              << "  migrate-activation [--profile ID] [--crypttab PATH] [--apply]\n"
              << "  show [--profile ID]\n"
              << "  export [--profile ID] --output PATH\n";
}

} // namespace

namespace btrfsbackup::cli {

int profile(
    const std::vector<std::string>& args,
    const fs::path& profile_config_dir,
    btrfsbackup::config::IConfigurationActivator& system_activator
) {
    fs::path etc_root =
        std::getenv("BTRFS_BACKUP_ETC_ROOT") ? std::getenv("BTRFS_BACKUP_ETC_ROOT") : profile_config_dir;
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
            profile_list(etc_root / "profiles", std::cout);
            return 0;
        }
        if (command == "wizard") {
            return profile_wizard(std::vector<std::string>(rest.begin() + 1, rest.end()));
        }
        if (command != "validate" && command != "render" && command != "save" && command != "show" && command != "export" && command != "migrate-activation") {
            fail("unknown command: " + command);
        }
        fs::path file;
        fs::path output_dir;
        fs::path crypttab = "/etc/crypttab";
        std::string profile_id = "default";
        bool apply = false;
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
            } else if (arg == "--crypttab" && i + 1 < rest.size()) {
                crypttab = rest[++i];
            } else if (arg == "--apply") {
                apply = true;
            } else if (arg == "-h" || arg == "--help") {
                usage();
                return 0;
            } else {
                fail("unknown option: " + arg);
            }
        }

        // TODO(4.0): Remove the legacy crypttab migration command after the 3.x transition window.
        if (command == "migrate-activation") {
            Profile migrated = btrfsbackup::platform::linux::migrate_target_activation_from_crypttab(
                btrfsbackup::platform::linux::get_profile(etc_root, profile_id),
                crypttab
            );
            if (apply) {
                if (geteuid() != 0 && etc_root == "/etc/btrfs-backup") {
                    fail("migration of system configuration must be run as root", 1);
                }
                const bool installs_system_configuration =
                    fs::absolute(etc_root).lexically_normal() == fs::path("/etc/btrfs-backup") &&
                    fs::absolute(udev_root).lexically_normal() == fs::path("/etc/udev/rules.d") &&
                    fs::absolute(systemd_root).lexically_normal() == fs::path("/etc/systemd/system") &&
                    fs::absolute(public_root).lexically_normal() ==
                        fs::path("/var/lib/btrfs-backup/public/profiles");
                btrfsbackup::config::NullConfigurationActivator null_activator;
                btrfsbackup::config::IConfigurationActivator& activator = installs_system_configuration
                    ? system_activator
                    : static_cast<btrfsbackup::config::IConfigurationActivator&>(null_activator);
                btrfsbackup::platform::linux::install_profile(
                    migrated,
                    {etc_root, udev_root, systemd_root, public_root},
                    activator
                );
                std::cout << "Migrated target activation for profile " << migrated.id.value()
                          << "; legacy crypttab was not modified\n";
            } else {
                std::cout << dump_json(profile_to_json(migrated));
            }
        } else if (command == "validate") {
            if (file.empty())
                fail("validate requires --file");
            btrfsbackup::config::ApplicationConfig config = btrfsbackup::platform::linux::load_application_config(etc_root);
            std::cout << btrfsbackup::config::dump_json(btrfsbackup::config::profile_to_json(btrfsbackup::platform::linux::validate_profile_file(file, config.paths().target_mount_root)));
        } else if (command == "render") {
            if (file.empty())
                fail("render requires --file");
            if (output_dir.empty())
                fail("render requires --output-dir");
            btrfsbackup::config::ApplicationConfig config = btrfsbackup::platform::linux::load_application_config(etc_root);
            btrfsbackup::config::Profile profile = btrfsbackup::platform::linux::validate_profile_file(file, config.paths().target_mount_root);
            btrfsbackup::platform::linux::render_profile(file, output_dir, config.paths().target_mount_root);
            std::cout << "Rendered profile " << profile.id.value() << " to " << output_dir << "\n";
        } else if (command == "save") {
            if (file.empty())
                fail("save requires --file");
            if (geteuid() != 0 && etc_root == "/etc/btrfs-backup") {
                fail("save to system configuration must be run as root", 1);
            }
            const bool installs_system_configuration = fs::absolute(etc_root).lexically_normal() == fs::path("/etc/btrfs-backup") && fs::absolute(udev_root).lexically_normal() == fs::path("/etc/udev/rules.d") && fs::absolute(systemd_root).lexically_normal() == fs::path("/etc/systemd/system") && fs::absolute(public_root).lexically_normal() == fs::path("/var/lib/btrfs-backup/public/profiles");
            btrfsbackup::config::NullConfigurationActivator null_activator;
            btrfsbackup::config::IConfigurationActivator& activator = installs_system_configuration
                ? system_activator
                : static_cast<btrfsbackup::config::IConfigurationActivator&>(null_activator);
            btrfsbackup::config::Profile profile = btrfsbackup::platform::linux::save_profile(file, {etc_root, udev_root, systemd_root, public_root}, activator);
            std::cout << "Saved profile " << profile.id.value() << "\n";
        } else if (command == "show") {
            std::cout << btrfsbackup::config::dump_json(btrfsbackup::config::profile_to_json(btrfsbackup::platform::linux::get_profile(etc_root, profile_id)));
        } else if (command == "export") {
            if (output_dir.empty())
                fail("export requires --output");
            btrfsbackup::config::Profile profile = btrfsbackup::platform::linux::export_profile(etc_root, profile_id, output_dir);
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

} // namespace btrfsbackup::cli
