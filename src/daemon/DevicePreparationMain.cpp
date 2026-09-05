// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/CredentialAdministrationService.hpp>
#include <daemon/control/CommandSystemdUnitController.hpp>
#include <daemon/control/DestructiveDeviceSafetyInspector.hpp>
#include <daemon/control/DevicePreparationTransactionStore.hpp>
#include <daemon/control/DevicePreparationUnitController.hpp>
#include <daemon/control/ExistingTargetInspector.hpp>
#include <daemon/control/SystemCredentialAdministrationBackend.hpp>
#include <daemon/control/SystemDeviceProvisioningBackend.hpp>

#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fcntl.h>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <vector>

#include <platform/linux/config/ApplicationConfig.hpp>
#include <platform/linux/config/FileProfileRepository.hpp>
#include <platform/linux/config/ProfileService.hpp>
#include <platform/linux/filesystem/SecretFile.hpp>
#include <platform/linux/process/PosixCommandRunner.hpp>
#include <platform/linux/storage/BlockDeviceMetadata.hpp>
#include <platform/linux/storage/MountInfo.hpp>
#include <platform/linux/storage/provisioning/BtrfsFilesystemFormatter.hpp>
#include <platform/linux/storage/CryptsetupOperations.hpp>
#include <platform/linux/storage/provisioning/ExistingTargetMountOperations.hpp>
#include <platform/linux/storage/SignatureOperations.hpp>
#include <platform/linux/storage/LibBtrfsOperations.hpp>
#include <platform/linux/storage/provisioning/PartitionTableOperations.hpp>
#include <platform/linux/storage/provisioning/SystemStorageTopologyReader.hpp>
#include <platform/linux/systemd/LinuxSystemConfigurationActivator.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::daemon {
namespace {

using btrfsbackup::platform::linux::OwnedFileDescriptor;

OwnedFileDescriptor read_secret_fifo(const fs::path& path) {
    OwnedFileDescriptor fifo(::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (!fifo.valid())
        throw std::runtime_error("cannot open device preparation secret channel");
    struct stat status{};
    if (::fstat(fifo.get(), &status) < 0 || !S_ISFIFO(status.st_mode) || status.st_uid != 0 ||
        (status.st_mode & (S_IRWXG | S_IRWXO)) != 0)
        throw std::runtime_error("device preparation secret channel is not trusted");
    OwnedFileDescriptor secret =
        btrfsbackup::platform::linux::filesystem::copy_secret_to_sealed_file(fifo.get());
    std::error_code error;
    fs::remove(path, error);
    return secret;
}

std::string block_source(std::string source) {
    const auto suffix = source.find('[');
    if (suffix != std::string::npos)
        source.erase(suffix);
    return source;
}

} // namespace

int run_device_preparation(int argc, char** argv) {
    std::string operation_id;
    fs::path transaction_root;
    try {
        if (argc != 2)
            throw std::runtime_error("usage: btrfs-backup-device-preparation OPERATION_ID");
        operation_id = argv[1];
        const fs::path config_root = "/etc/btrfs-backup";
        const auto application_config =
            btrfsbackup::platform::linux::config::load_application_config(config_root);
        const auto& paths = application_config.paths();
        transaction_root = paths.state_root / "device-preparations";
        const auto transaction =
            btrfsbackup::daemon::control::DevicePreparationTransactionStore(transaction_root)
                .load(operation_id);
        const auto host_mounts = btrfsbackup::platform::linux::storage::read_mount_table(
            "/proc/1/mountinfo",
            [](const std::string&) { return std::string{}; }
        );
        std::map<std::string, std::string> source_filesystems;
        for (const auto& source : transaction.sources) {
            const auto source_mount = btrfsbackup::backup::mount_at(
                host_mounts,
                source.mount_root
            );
            if (!source_mount.has_value() || source_mount->fstype != "btrfs")
                throw std::runtime_error("device preparation source mount is unavailable");
            const std::string source_device = block_source(source_mount->source);
            const auto [entry, inserted] = source_filesystems.emplace(
                source_device,
                source.filesystem_uuid
            );
            if (!inserted && entry->second != source.filesystem_uuid)
                throw std::runtime_error("device preparation source identity is inconsistent");
        }
        const auto source_uuid_resolver = [source_filesystems](const std::string& device) {
            const auto source = source_filesystems.find(device);
            return source == source_filesystems.end() ? std::string{} : source->second;
        };

        btrfsbackup::platform::linux::process::PosixCommandRunner commands;
        btrfsbackup::platform::linux::storage::provisioning::CommandBtrfsFilesystemFormatter btrfs_formatter(commands);
        btrfsbackup::platform::linux::storage::LibBtrfsOperations btrfs;
        btrfsbackup::platform::linux::systemd::LinuxSystemConfigurationActivator activator;
        btrfsbackup::platform::linux::storage::CryptsetupOperations cryptsetup;
        const btrfsbackup::daemon::control::CredentialAdministrationRoots roots{
            .config_root = config_root,
            .metadata_root = config_root / "credentials",
            .key_root = config_root / "keys",
            .lock_root = "/run/btrfs-backup/locks",
            .udev_root = "/etc/udev/rules.d",
            .systemd_root = "/etc/systemd/system",
            .public_root = "/var/lib/btrfs-backup/public/profiles",
        };
        btrfsbackup::daemon::control::SystemCredentialAdministrationBackend credentials(
            roots,
            cryptsetup,
            activator
        );
        btrfsbackup::platform::linux::config::FileProfileRepository profiles(
            config_root,
            application_config
        );
        const auto configured_targets = [&profiles, &config_root] {
            std::vector<btrfsbackup::provisioning::ConfiguredBackupTargetIdentity> result;
            for (const auto& profile_id :
                 btrfsbackup::platform::linux::config::list_profiles(config_root / "profiles")) {
                const auto& target = profiles.get(btrfsbackup::ProfileId{profile_id}).profile.target;
                result.push_back({target.partition_uuid.value(), target.luks_uuid.value()});
            }
            return result;
        };
        btrfsbackup::platform::linux::storage::provisioning::SystemStorageTopologyReader storage_topology(
            {.mountinfo = "/proc/1/mountinfo"},
            configured_targets
        );
        btrfsbackup::platform::linux::storage::LibblkidSignatureOperations signature_operations;
        btrfsbackup::platform::linux::storage::LibblkidBlockDeviceMetadataReader metadata_reader;
        btrfsbackup::platform::linux::storage::provisioning::LibfdiskPartitionTableOperations partition_tables;
        btrfsbackup::daemon::control::DestructiveDeviceSafetyInspector safety(storage_topology);
        btrfsbackup::platform::linux::storage::provisioning::LibmountExistingTargetMountOperations existing_target_mounts;
        btrfsbackup::daemon::control::ExistingTargetInspector existing_target_inspector(
            cryptsetup,
            metadata_reader,
            existing_target_mounts,
            btrfs
        );
        btrfsbackup::daemon::control::CommandSystemdUnitController systemd_units(commands);
        btrfsbackup::daemon::control::SystemdDevicePreparationUnitController units(systemd_units);
        btrfsbackup::daemon::control::SystemDeviceProvisioningBackend backend(
            roots,
            paths.target_mount_root,
            "/proc/1/mountinfo",
            transaction_root,
            storage_topology,
            btrfs_formatter,
            signature_operations,
            metadata_reader,
            partition_tables,
            cryptsetup,
            btrfs,
            activator,
            credentials,
            safety,
            units,
            false,
            &existing_target_inspector,
            paths.status_root.parent_path() / "target-inspections",
            source_uuid_resolver,
            {},
            false
        );

        if (transaction.status.state == "interrupted") {
            backend.recover_operation(operation_id);
            return 0;
        }
        OwnedFileDescriptor secret = read_secret_fifo(units.secret_path(operation_id));
        backend.execute_operation(operation_id, secret.get());
        return 0;
    } catch (const std::exception& error) {
        if (!operation_id.empty() && !transaction_root.empty()) {
            try {
                btrfsbackup::daemon::control::DevicePreparationTransactionStore store(transaction_root);
                auto transaction = store.load(operation_id);
                if (transaction.status.state == "queued" || transaction.status.state == "running") {
                    transaction.status.state = "failed";
                    transaction.status.error_code = "device-preparation.helper-failed";
                    transaction.status.recovery_action =
                        "Inspect the helper journal and the recorded device before retrying.";
                    transaction.status.can_cancel = false;
                    transaction.updated_at = std::chrono::duration_cast<std::chrono::seconds>(
                                                 std::chrono::system_clock::now().time_since_epoch()
                    )
                                                 .count();
                    store.save(transaction);
                }
            } catch (...) {
            }
        }
        std::cerr << "btrfs-backup-device-preparation: " << error.what() << '\n';
        return 1;
    }
}

} // namespace btrfsbackup::daemon

int main(int argc, char** argv) {
    return btrfsbackup::daemon::run_device_preparation(argc, argv);
}
