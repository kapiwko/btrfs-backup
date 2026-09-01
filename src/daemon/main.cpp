// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/dbus/ManagerDbusServer.hpp>
#include <daemon/control/CommandSystemdUnitController.hpp>
#include <daemon/control/DestructiveDeviceSafetyInspector.hpp>
#include <daemon/control/DevicePreparationUnitController.hpp>
#include <daemon/ManagerAuditLog.hpp>
#include <daemon/control/SystemOperationalControlBackend.hpp>
#include <daemon/control/SystemProfileAdministrationBackend.hpp>
#include <daemon/control/SystemCredentialAdministrationBackend.hpp>
#include <daemon/control/SystemDeviceProvisioningBackend.hpp>
#include <daemon/control/SystemBrowseSessionBackend.hpp>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include <platform/linux/config/ApplicationConfig.hpp>
#include <platform/linux/config/FileProfileRepository.hpp>
#include <platform/linux/systemd/LinuxSystemConfigurationActivator.hpp>
#include <platform/linux/process/PosixCommandRunner.hpp>
#include <platform/linux/filesystem/PosixDurableFileOperations.hpp>
#include <platform/linux/storage/MountInfo.hpp>
#include <platform/linux/storage/LibBtrfsOperations.hpp>
#include <platform/linux/storage/CryptsetupOperations.hpp>
#include <state/persistence/FileRunStateRepository.hpp>

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
        fs::path audit_log_path = "/var/log/btrfs-backup/manager-audit.jsonl";
        fs::path udev_root = "/etc/udev/rules.d";
        fs::path systemd_root = "/etc/systemd/system";
        bool skip_configuration_activation = false;
        for (int index = 1; index < argc; ++index) {
            if (std::string(argv[index]) == "--config-root") {
                config_root = absolute_path(require_value(argc, argv, index), "--config-root");
            }
        }

        const btrfsbackup::config::ApplicationConfig application_config =
            btrfsbackup::platform::linux::config::load_application_config(config_root);
        const btrfsbackup::config::ApplicationPaths& configured = application_config.paths();
        btrfsbackup::daemon::ManagerPaths paths{
            .config_root = config_root,
            .public_profile_root = "/var/lib/btrfs-backup/public/profiles",
            .status_root = configured.status_root,
            .history_root = configured.history_root,
            .state_root = configured.state_root,
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
            } else if (argument == "--audit-log") {
                audit_log_path = absolute_path(require_value(argc, argv, index), argument.c_str());
            } else if (argument == "--udev-root") {
                udev_root = absolute_path(require_value(argc, argv, index), argument.c_str());
            } else if (argument == "--systemd-root") {
                systemd_root = absolute_path(require_value(argc, argv, index), argument.c_str());
            } else if (argument == "--skip-configuration-activation") {
                skip_configuration_activation = true;
            } else if (argument == "--help") {
                std::cout
                    << "Usage: btrfs-backupd [--bus-address ADDRESS] [--config-root PATH]\n"
                    << "                         [--public-profile-root PATH] [--status-root PATH]\n"
                    << "                         [--history-root PATH] [--target-mount-root PATH]\n"
                    << "                         [--mapper-root PATH] [--mountinfo PATH]\n"
                    << "                         [--audit-log PATH]\n";
                return 0;
            } else {
                throw std::runtime_error("unknown option: " + argument);
            }
        }

        btrfsbackup::daemon::ManagerService service(paths);
        btrfsbackup::platform::linux::config::FileProfileRepository profiles(config_root, application_config);
        btrfsbackup::platform::linux::filesystem::PosixDurableFileOperations durable_files;
        btrfsbackup::state::FileRunStateRepository state(configured, durable_files);
        btrfsbackup::platform::linux::process::PosixCommandRunner commands;
        btrfsbackup::daemon::control::CommandSystemdUnitController units(commands);
        btrfsbackup::daemon::control::SystemOperationalControlBackend operational_backend(profiles, state, units);
        btrfsbackup::platform::linux::storage::LinuxMountInspector mounts(paths.mountinfo_path);
        btrfsbackup::daemon::control::SystemBrowseSessionBackend browse_session_backend(profiles, mounts, units);
        btrfsbackup::platform::linux::systemd::LinuxSystemConfigurationActivator configuration_activator;
        btrfsbackup::config::NullConfigurationActivator null_configuration_activator;
        btrfsbackup::config::IConfigurationActivator& selected_configuration_activator =
            skip_configuration_activation
            ? static_cast<btrfsbackup::config::IConfigurationActivator&>(null_configuration_activator)
            : static_cast<btrfsbackup::config::IConfigurationActivator&>(configuration_activator);
        btrfsbackup::platform::linux::storage::LibBtrfsOperations btrfs;
        btrfsbackup::daemon::control::SystemProfileAdministrationBackend profile_administration_backend(
            {
                .etc_root = config_root,
                .udev_root = udev_root,
                .systemd_root = systemd_root,
                .public_root = paths.public_profile_root,
            },
            paths.target_mount_root,
            paths.mountinfo_path,
            btrfs,
            selected_configuration_activator
        );
        btrfsbackup::platform::linux::storage::CryptsetupOperations cryptsetup(commands);
        const btrfsbackup::daemon::control::CredentialAdministrationRoots credential_roots{
            .config_root = config_root,
            .metadata_root = config_root / "credentials",
            .key_root = config_root / "keys",
            .lock_root = "/run/btrfs-backup/locks",
            .udev_root = udev_root,
            .systemd_root = systemd_root,
            .public_root = paths.public_profile_root,
        };
        btrfsbackup::daemon::control::SystemCredentialAdministrationBackend credential_administration_backend(
            credential_roots,
            cryptsetup,
            selected_configuration_activator
        );
        btrfsbackup::daemon::control::DestructiveDeviceSafetyInspector destructive_device_safety(commands);
        btrfsbackup::daemon::control::SystemdDevicePreparationUnitController device_preparation_units(commands);
        btrfsbackup::daemon::control::SystemDeviceProvisioningBackend device_provisioning_backend(
            credential_roots,
            paths.target_mount_root,
            paths.mountinfo_path,
            paths.state_root / "device-preparations",
            commands,
            btrfs,
            selected_configuration_activator,
            credential_administration_backend,
            destructive_device_safety,
            device_preparation_units
        );
        btrfsbackup::daemon::FileManagerAuditLog audit_log(audit_log_path);
        return btrfsbackup::daemon::dbus::run_dbus_server(
            service,
            operational_backend,
            profile_administration_backend,
            credential_administration_backend,
            device_provisioning_backend,
            browse_session_backend,
            audit_log,
            paths,
            bus_address
        );
    } catch (const std::exception& exception) {
        std::cerr << "btrfs-backupd: " << exception.what() << '\n';
        return 1;
    }
}
