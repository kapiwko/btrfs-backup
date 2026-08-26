// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <cli/installation_command.hpp>
#include <platform/linux/config/application_config.hpp>
#include <platform/linux/config/installation_service.hpp>

namespace fs = std::filesystem;

namespace {

fs::path application_config_root() {
    return std::getenv("BTRFS_BACKUP_PROFILE_CONFIG_DIR")
        ? fs::path(std::getenv("BTRFS_BACKUP_PROFILE_CONFIG_DIR"))
        : fs::path("/etc/btrfs-backup");
}

[[noreturn]] void fail(const std::string& message, int code = 2) {
    std::cerr << "btrfs-backupctl: " << message << '\n';
    std::exit(code);
}

std::string arg_value(std::size_t& index, const std::vector<std::string>& args, const std::string& option) {
    if (index + 1 >= args.size()) {
        fail(option + " requires a value");
    }
    return args[++index];
}

} // namespace

namespace btrfsbackup::command {

int render_installation(const std::vector<std::string>& args) {
    fs::path file;
    fs::path output_dir;
    btrfsbackup::InstallationRenderOptions options;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--file") {
            file = arg_value(i, args, arg);
        } else if (arg == "--output-dir") {
            output_dir = arg_value(i, args, arg);
        } else if (arg == "--backup-command") {
            options.backup_command = arg_value(i, args, arg);
        } else if (arg == "--eject-script") {
            options.eject_script = arg_value(i, args, arg);
        } else if (arg == "--keyfile") {
            options.keyfile = arg_value(i, args, arg);
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: btrfs-backupctl installation render --file PATH --output-dir PATH [--backup-command COMMAND] [--eject-script PATH] [--keyfile PATH]\n";
            return 0;
        } else {
            fail("unknown installation render option: " + arg);
        }
    }
    if (file.empty()) fail("installation render requires --file");
    if (output_dir.empty()) fail("installation render requires --output-dir");

    btrfsbackup::ApplicationConfig config = btrfsbackup::load_application_config(application_config_root());
    btrfsbackup::render_installation({file, output_dir, options, config.paths().target_mount_root});
    return 0;
}

int validate_installation(const std::vector<std::string>& args) {
    fs::path rendered_root;
    bool active = false;
    std::string profile_id = "default";
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--rendered-root") {
            rendered_root = arg_value(i, args, arg);
        } else if (arg == "--active") {
            active = true;
        } else if (arg == "--profile") {
            profile_id = arg_value(i, args, arg);
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: btrfs-backupctl installation validate (--rendered-root PATH | --active [--profile ID])\n";
            return 0;
        } else {
            fail("unknown installation validate option: " + arg);
        }
    }
    if (active == !rendered_root.empty()) {
        fail("installation validate requires exactly one of --rendered-root or --active");
    }
    if (active) {
        if (geteuid() != 0) {
            fail("active installation validation must be run as root", 1);
        }
        btrfsbackup::validate_active_installation_for(profile_id);
    } else {
        btrfsbackup::ApplicationConfig config = btrfsbackup::load_application_config(application_config_root());
        btrfsbackup::validate_rendered_installation_at(rendered_root, config.paths().target_mount_root);
    }
    return 0;
}

void installation_usage() {
    std::cout << "Usage: btrfs-backupctl installation COMMAND\n"
              << "\nCommands:\n"
              << "  render --file PATH --output-dir PATH\n"
              << "  validate (--rendered-root PATH | --active [--profile ID])\n";
}

int installation(const std::vector<std::string>& args) {
    if (args.empty()) {
        installation_usage();
        return 2;
    }
    const std::string& command = args[0];
    std::vector<std::string> rest(args.begin() + 1, args.end());
    if (command == "render") {
        return render_installation(rest);
    }
    if (command == "validate") {
        return validate_installation(rest);
    }
    if (command == "-h" || command == "--help") {
        installation_usage();
        return 0;
    }
    fail("unknown installation command: " + command);
}

} // namespace btrfsbackup::command
