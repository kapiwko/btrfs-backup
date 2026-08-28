// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/dbus_server.hpp>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include <platform/linux/config/application_config.hpp>

namespace fs = std::filesystem;

namespace {

fs::path absolute_path(const std::string& value, const char* option) {
    fs::path result = value;
    if (!result.is_absolute())
        throw std::runtime_error(std::string(option) + " requires an absolute path");
    return result.lexically_normal();
}

std::string require_value(int argc, char** argv, int& index) {
    if (index + 1 >= argc)
        throw std::runtime_error(std::string(argv[index]) + " requires a value");
    return argv[++index];
}

} // namespace

int main(int argc, char** argv) {
    try {
        fs::path config_root = "/etc/btrfs-backup";
        for (int index = 1; index < argc; ++index) {
            if (std::string(argv[index]) == "--config-root") {
                config_root = absolute_path(require_value(argc, argv, index), "--config-root");
            }
        }

        const btrfsbackup::config::ApplicationPaths configured = btrfsbackup::platform::linux::load_application_config(config_root).paths();
        btrfsbackup::daemon::ManagerPaths paths{
            .config_root = config_root,
            .public_profile_root = "/var/lib/btrfs-backup/public/profiles",
            .status_root = configured.status_root,
            .history_root = configured.history_root,
            .target_mount_root = configured.target_mount_root,
        };
        std::string bus_address;
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--config-root") {
                ++index;
            } else if (argument == "--bus-address") {
                bus_address = require_value(argc, argv, index);
                if (bus_address.size() > 4096)
                    throw std::runtime_error("--bus-address exceeds 4096 bytes");
            } else if (argument == "--public-profile-root") {
                paths.public_profile_root = absolute_path(require_value(argc, argv, index), argument.c_str());
            } else if (argument == "--status-root") {
                paths.status_root = absolute_path(require_value(argc, argv, index), argument.c_str());
            } else if (argument == "--history-root") {
                paths.history_root = absolute_path(require_value(argc, argv, index), argument.c_str());
            } else if (argument == "--target-mount-root") {
                paths.target_mount_root = absolute_path(require_value(argc, argv, index), argument.c_str());
            } else if (argument == "--mapper-root") {
                paths.mapper_root = absolute_path(require_value(argc, argv, index), argument.c_str());
            } else if (argument == "--mountinfo") {
                paths.mountinfo_path = absolute_path(require_value(argc, argv, index), argument.c_str());
            } else if (argument == "--help") {
                std::cout
                    << "Usage: btrfs-backupd [--bus-address ADDRESS] [--config-root PATH]\n"
                    << "                         [--public-profile-root PATH] [--status-root PATH]\n"
                    << "                         [--history-root PATH] [--target-mount-root PATH]\n"
                    << "                         [--mapper-root PATH] [--mountinfo PATH]\n";
                return 0;
            } else {
                throw std::runtime_error("unknown option: " + argument);
            }
        }

        btrfsbackup::daemon::ManagerService service(std::move(paths));
        return btrfsbackup::daemon::run_dbus_server(service, bus_address);
    } catch (const std::exception& exception) {
        std::cerr << "btrfs-backupd: " << exception.what() << '\n';
        return 1;
    }
}
