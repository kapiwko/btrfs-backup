// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/DevicePreparationExecutor.hpp>

#include <chrono>
#include <iostream>
#include <utility>
#include <unistd.h>

#include <backup/ports/IBtrfsOperations.hpp>
#include <backup/ports/ICommandRunner.hpp>
#include <config/ports/ConfigurationActivator.hpp>
#include <core/Errors.hpp>
#include <core/Identifiers.hpp>
#include <daemon/control/DestructiveDeviceSafetyInspector.hpp>
#include <daemon/control/ExistingTargetInspector.hpp>
#include <daemon/control/ProvisioningDeviceEnumerator.hpp>
#include <daemon/dbus/ManagerErrors.hpp>
#include <platform/linux/config/ProfileService.hpp>
#include <platform/linux/filesystem/FileLock.hpp>
#include <platform/linux/filesystem/SecretFile.hpp>
#include <platform/linux/filesystem/TrustedDirectory.hpp>
#include <platform/linux/storage/BlockDeviceMetadata.hpp>
#include <platform/linux/storage/CryptsetupOperations.hpp>
#include <platform/linux/storage/PartitionTableOperations.hpp>
#include <platform/linux/storage/SignatureOperations.hpp>

namespace fs = std::filesystem;

namespace btrfsbackup::daemon::control {
namespace {

using platform::linux::OwnedFileDescriptor;

std::int64_t system_time_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch()
    )
        .count();
}

void require_success(
    backup::ICommandRunner& commands,
    const std::vector<std::string>& argv,
    const backup::ControlledCommandOptions& options,
    const char* operation
) {
    const auto result = commands.run_controlled(argv, options);
    if (result.exit_code != 0 || result.cancelled || result.timed_out)
        throw ValidationError(std::string(operation) + " failed");
}

void rewind_secret(int fd) {
    if (::lseek(fd, 0, SEEK_SET) < 0)
        throw ValidationError("cannot rewind device preparation secret");
}

void remove_inspection_mount_point(const fs::path& mount_point) {
    std::error_code error;
    if (!fs::remove(mount_point, error) || error)
        throw ValidationError("cannot remove existing target inspection mount point");
}

} // namespace

DevicePreparationExecutor::DevicePreparationExecutor(
    CredentialAdministrationRoots roots,
    fs::path target_mount_root,
    backup::ICommandRunner& commands,
    platform::linux::storage::ISignatureOperations& signatures,
    platform::linux::storage::IBlockDeviceMetadataReader& metadata,
    platform::linux::storage::IPartitionTableOperations& partition_tables,
    platform::linux::storage::ICryptsetupOperations& cryptsetup,
    backup::IBtrfsOperations& btrfs,
    config::IConfigurationActivator& configuration_activator,
    ICredentialAdministrationBackend& credentials,
    IDestructiveDeviceSafetyInspector& safety_inspector,
    DevicePreparationTransactionStore& transactions,
    ProvisioningDeviceEnumerator& devices,
    IExistingTargetInspector* existing_target_inspector,
    fs::path inspection_mount_root
)
    : roots_(std::move(roots)),
      commands_(commands),
      signatures_(signatures),
      metadata_(metadata),
      partition_tables_(partition_tables),
      cryptsetup_(cryptsetup),
      btrfs_(btrfs),
      activator_(configuration_activator),
      credentials_(credentials),
      safety_inspector_(safety_inspector),
      transactions_(transactions),
      devices_(devices),
      plan_builder_(std::move(target_mount_root)),
      existing_target_inspector_(existing_target_inspector),
      inspection_mount_root_(std::move(inspection_mount_root)) {
}

void DevicePreparationExecutor::update(
    const std::string& operation_id,
    const TransactionMutator& mutator
) {
    DevicePreparationTransaction transaction = transactions_.load(operation_id);
    mutator(transaction);
    transaction.updated_at = system_time_seconds();
    transactions_.save(transaction);
}

void DevicePreparationExecutor::phase(
    const std::string& operation_id,
    const std::string& value,
    bool can_cancel
) {
    update(operation_id, [&](auto& transaction) {
        transaction.status.state = "running";
        transaction.status.phase = value;
        transaction.status.can_cancel = can_cancel;
    });
}

void DevicePreparationExecutor::completed(const std::string& operation_id, const std::string& value) {
    update(operation_id, [&](auto& transaction) { transaction.last_completed_phase = value; });
}

void DevicePreparationExecutor::execute(const std::string& operation_id, int passphrase_fd) {
    const DevicePreparationTransaction initial = transactions_.load(operation_id);
    if (initial.status.state != "queued" && initial.status.state != "running")
        throw ValidationError("device preparation transaction is not executable");
    OwnedFileDescriptor passphrase = platform::linux::filesystem::copy_secret_to_sealed_file(passphrase_fd);
    std::string mapper;
    try {
        phase(operation_id, "inspect", true);
        if (initial.target.mode == provisioning::ProvisioningMode::EraseWholeDevice) {
            const ProvisioningDevice selected = devices_.revalidate(initial.device);
            if (selected.mounted)
                throw ValidationError("selected device or one of its partitions is mounted");
        } else if (initial.target.mode == provisioning::ProvisioningMode::CreatePartitionInUnallocatedSpace) {
            if (!initial.target.free_region.has_value() || !initial.target.planned_partition_geometry.has_value())
                throw ValidationError("free-space preparation transaction is incomplete");
        } else if ((initial.target.mode != provisioning::ProvisioningMode::ReformatExistingPartition && initial.target.mode != provisioning::ProvisioningMode::AdoptExistingTarget) || !initial.target.partition.has_value()) {
            throw ValidationError("device preparation transaction mode is not executable");
        }
        if (!btrfs_.is_subvolume(initial.source_subvolume))
            throw ValidationError("selected source is not a Btrfs subvolume");
        completed(operation_id, "inspect");
        platform::linux::filesystem::FileLock device_lock(roots_.lock_root / "device-provisioning.lock");
        if (!device_lock.try_acquire())
            throw dbus::ManagerOperationError(dbus::ManagerErrorCode::Busy, "another device is being prepared");

        if (initial.target.mode == provisioning::ProvisioningMode::EraseWholeDevice) {
            const ProvisioningDevice before_wipe = devices_.revalidate(initial.device);
            if (before_wipe.mounted)
                throw ValidationError("selected device or one of its partitions became mounted");
        }
        const std::vector<std::string> safety_reasons = safety_inspector_.inspect(initial.device, initial.target);
        if (!safety_reasons.empty())
            throw dbus::ManagerOperationError(
                dbus::ManagerErrorCode::Conflict,
                "selected device is not safe for destructive preparation: " + safety_reasons.front()
            );
        if (initial.target.mode == provisioning::ProvisioningMode::AdoptExistingTarget) {
            if (existing_target_inspector_ == nullptr || !initial.target.expected_inspection.has_value() ||
                inspection_mount_root_.empty())
                throw ValidationError("existing target adoption transaction is incomplete");
            phase(operation_id, "verify-existing-target", false);
            platform::linux::filesystem::ensure_trusted_directory(
                inspection_mount_root_,
                0700,
                inspection_mount_root_.parent_path(),
                ::geteuid()
            );
            const fs::path mount_point = inspection_mount_root_ / operation_id;
            platform::linux::filesystem::ensure_trusted_directory(
                mount_point,
                0700,
                inspection_mount_root_,
                ::geteuid()
            );
            update(operation_id, [&](auto& transaction) {
                transaction.mapper = operation_id;
                transaction.inspection_mount_point = mount_point.string();
                transaction.cleanup_result = "pending";
            });
            provisioning::ExistingTargetInspectionSummary inspected;
            try {
                rewind_secret(passphrase.get());
                inspected = existing_target_inspector_->inspect(
                    *initial.target.partition,
                    operation_id,
                    mount_point,
                    passphrase.get()
                );
                remove_inspection_mount_point(mount_point);
                update(operation_id, [](auto& transaction) {
                    transaction.mapper.clear();
                    transaction.inspection_mount_point.clear();
                    transaction.cleanup_result = "inspection-cleaned";
                });
            } catch (...) {
                std::error_code cleanup_error;
                static_cast<void>(fs::remove(mount_point, cleanup_error));
                update(operation_id, [&](auto& transaction) {
                    transaction.mapper.clear();
                    transaction.inspection_mount_point.clear();
                    transaction.cleanup_result = cleanup_error ? "inspection-directory-remove-failed" : "inspection-cleaned";
                });
                throw;
            }
            if (inspected != *initial.target.expected_inspection)
                throw dbus::ManagerOperationError(
                    dbus::ManagerErrorCode::Conflict,
                    "existing target changed since the accepted inspection"
                );
            update(operation_id, [&](auto& transaction) {
                transaction.partition = transaction.target.partition->identity.display_path;
                transaction.partition_uuid = inspected.partition_uuid;
                transaction.luks_uuid = inspected.luks_uuid;
                transaction.btrfs_uuid = inspected.btrfs_uuid;
                transaction.last_completed_phase = "verify-existing-target";
            });

            phase(operation_id, "write-profile", false);
            update(operation_id, [](auto& transaction) { transaction.configuration_state = "in-progress"; });
            config::Profile profile = plan_builder_.build(
                initial,
                inspected.luks_uuid,
                inspected.btrfs_uuid,
                inspected.partition_uuid
            );
            profile.enabled = false;
            platform::linux::config::install_profile(
                profile,
                {roots_.config_root, roots_.udev_root, roots_.systemd_root, roots_.public_root},
                activator_
            );
            update(operation_id, [](auto& transaction) {
                transaction.configuration_state = "installed";
                transaction.credentials_state = "not-applicable";
                transaction.last_completed_phase = "write-profile";
                transaction.status.state = "succeeded";
                transaction.status.phase = "complete";
                transaction.status.can_cancel = false;
                transaction.status.recovery_action.clear();
            });
            return;
        }
        backup::ControlledCommandOptions standard;
        std::string partition;
        if (initial.target.mode == provisioning::ProvisioningMode::EraseWholeDevice) {
            phase(operation_id, "wipe-signatures", false);
            signatures_.wipe_all(initial.device.path, initial.device.major_minor);
            completed(operation_id, "wipe-signatures");

            phase(operation_id, "partition", false);
            partition_tables_.replace_with_single_gpt_partition(initial.device.path, initial.device.major_minor);
            partition = devices_.only_partition(initial.device);
            update(operation_id, [&](auto& transaction) {
                transaction.partition = partition;
                transaction.last_completed_phase = "partition";
            });
        } else if (initial.target.mode == provisioning::ProvisioningMode::CreatePartitionInUnallocatedSpace) {
            phase(operation_id, "backup-partition-table", false);
            const std::string partition_table_backup = partition_tables_.snapshot_partition_table(
                initial.device.path,
                initial.device.major_minor,
                initial.target.device.partition_table.identifier,
                initial.target.device.logical_sector_size
            );
            if (partition_table_backup.empty())
                throw ValidationError("partition table backup is empty");
            update(operation_id, [&](auto& transaction) {
                transaction.partition_table_backup = partition_table_backup;
                transaction.last_completed_phase = "backup-partition-table";
            });

            phase(operation_id, "partition", false);
            const auto& free_region = *initial.target.free_region;
            const auto& planned = *initial.target.planned_partition_geometry;
            partition = partition_tables_
                            .create_partition_in_free_space(
                                initial.device.path,
                                initial.device.major_minor,
                                initial.target.device.partition_table.identifier,
                                initial.target.device.logical_sector_size,
                                free_region.start_sector,
                                free_region.sector_count,
                                {
                                    .start_sector = planned.start_sector,
                                    .sector_count = planned.sector_count,
                                    .partition_number = planned.partition_number,
                                }
                            )
                            .string();
            update(operation_id, [&](auto& transaction) {
                transaction.partition = partition;
                transaction.last_completed_phase = "partition";
            });
        } else {
            phase(operation_id, "wipe-signatures", false);
            const auto& selected_partition = *initial.target.partition;
            partition = selected_partition.identity.display_path;
            signatures_.wipe_all(
                partition,
                selected_partition.identity.major_minor,
                platform::linux::storage::SignatureExpectation{
                    .type = selected_partition.filesystem.type,
                    .version = selected_partition.filesystem.version,
                    .label = selected_partition.filesystem.label,
                    .uuid = selected_partition.filesystem.uuid,
                }
            );
            completed(operation_id, "wipe-signatures");
        }

        phase(operation_id, "luks-format", false);
        const std::string luks_uuid = cryptsetup_.format_luks2(partition, passphrase.get());
        update(operation_id, [&](auto& transaction) {
            transaction.luks_uuid = luks_uuid;
            transaction.last_completed_phase = "luks-format";
        });

        phase(operation_id, "open", false);
        rewind_secret(passphrase.get());
        mapper = "btrfs-backup-" + initial.status.profile_id;
        cryptsetup_.open_luks2(partition, mapper, passphrase.get());
        update(operation_id, [&](auto& transaction) {
            transaction.mapper = mapper;
            transaction.last_completed_phase = "open";
            transaction.cleanup_result = "pending";
        });
        const std::string mapper_path = "/dev/mapper/" + mapper;

        phase(operation_id, "mkfs-btrfs", false);
        require_success(
            commands_,
            {"mkfs.btrfs", "--force", "--label", initial.profile_name, mapper_path},
            standard,
            "creating Btrfs filesystem"
        );
        const auto mapper_metadata = metadata_.read(mapper_path);
        const auto partition_metadata = metadata_.read(partition);
        if (mapper_metadata.filesystem_uuid.empty())
            throw ValidationError("Btrfs UUID is unavailable after formatting");
        if (partition_metadata.partition_uuid.empty())
            throw ValidationError("partition UUID is unavailable after formatting");
        const std::string& btrfs_uuid = mapper_metadata.filesystem_uuid;
        const std::string& partition_uuid = partition_metadata.partition_uuid;
        update(operation_id, [&](auto& transaction) {
            transaction.btrfs_uuid = btrfs_uuid;
            transaction.partition_uuid = partition_uuid;
            transaction.last_completed_phase = "mkfs-btrfs";
        });

        phase(operation_id, "close", false);
        cryptsetup_.close(mapper);
        mapper.clear();
        update(operation_id, [](auto& transaction) {
            transaction.mapper.clear();
            transaction.cleanup_result = "mapper-closed";
            transaction.last_completed_phase = "close";
        });

        phase(operation_id, "write-profile", false);
        update(operation_id, [](auto& transaction) { transaction.configuration_state = "in-progress"; });
        config::Profile profile = plan_builder_.build(initial, luks_uuid, btrfs_uuid, partition_uuid);
        platform::linux::config::install_profile(
            profile,
            {roots_.config_root, roots_.udev_root, roots_.systemd_root, roots_.public_root},
            activator_
        );
        update(operation_id, [](auto& transaction) {
            transaction.configuration_state = "installed";
            transaction.last_completed_phase = "write-profile";
            transaction.credentials_state = "in-progress";
        });
        credentials_.register_initial_passphrase(ProfileId{initial.status.profile_id}, 0, initial.passphrase_label);
        if (initial.create_automatic_key) {
            rewind_secret(passphrase.get());
            credentials_.generate_key(
                ProfileId{initial.status.profile_id},
                passphrase.get(),
                "Automatic backup key",
                true
            );
        }
        update(operation_id, [](auto& transaction) {
            transaction.credentials_state = "installed";
            transaction.last_completed_phase = "credentials";
            transaction.status.state = "succeeded";
            transaction.status.phase = "complete";
            transaction.status.can_cancel = false;
            transaction.status.recovery_action.clear();
        });
    } catch (const std::exception& error) {
        const bool cleanup_required = !mapper.empty();
        bool cleanup_ok = !cleanup_required;
        try {
            if (cleanup_required)
                cryptsetup_.close(mapper);
            cleanup_ok = true;
        } catch (...) {
            cleanup_ok = false;
        }
        try {
            update(operation_id, [&](auto& transaction) {
                std::cerr << "Device preparation " << operation_id << " failed during "
                          << transaction.status.phase << ": " << error.what() << '\n';
                transaction.status.state = "failed";
                transaction.status.error_code = "device-preparation." + transaction.status.phase + "-failed";
                transaction.status.recovery_action = initial.target.mode == provisioning::ProvisioningMode::AdoptExistingTarget
                    ? "Rescan and inspect the existing target before creating a new adoption plan."
                    : "Inspect the recorded device artifacts and complete or remove partial structures manually.";
                transaction.status.can_cancel = false;
                transaction.cleanup_result = !cleanup_required
                    ? "not-required"
                    : (cleanup_ok ? "mapper-closed" : "mapper-close-failed");
                if (cleanup_ok)
                    transaction.mapper.clear();
            });
        } catch (const std::exception& persistence_error) {
            std::cerr << "Cannot persist failed device preparation: " << persistence_error.what() << '\n';
        }
    } catch (...) {
        const bool cleanup_required = !mapper.empty();
        bool cleanup_ok = !cleanup_required;
        try {
            if (cleanup_required)
                cryptsetup_.close(mapper);
            cleanup_ok = true;
        } catch (...) {
            cleanup_ok = false;
        }
        try {
            update(operation_id, [&](auto& transaction) {
                transaction.status.state = "failed";
                transaction.status.error_code = "device-preparation.unknown-failed";
                transaction.status.recovery_action = initial.target.mode == provisioning::ProvisioningMode::AdoptExistingTarget
                    ? "Rescan and inspect the existing target before creating a new adoption plan."
                    : "Inspect and repair the recorded device artifacts manually.";
                transaction.status.can_cancel = false;
                transaction.cleanup_result = !cleanup_required
                    ? "not-required"
                    : (cleanup_ok ? "mapper-closed" : "mapper-close-failed");
                if (cleanup_ok)
                    transaction.mapper.clear();
            });
        } catch (...) {
        }
    }
}

void DevicePreparationExecutor::recover(const std::string& operation_id) {
    DevicePreparationTransaction transaction = transactions_.load(operation_id);
    if (transaction.status.state != "interrupted")
        throw ValidationError("device preparation transaction does not require cleanup");
    if (transaction.mapper.empty() && transaction.inspection_mount_point.empty())
        return;
    if (transaction.target.mode == provisioning::ProvisioningMode::AdoptExistingTarget) {
        if (existing_target_inspector_ == nullptr || transaction.mapper.empty() ||
            transaction.inspection_mount_point.empty())
            throw ValidationError("existing target cleanup state is incomplete");
        try {
            existing_target_inspector_->cleanup_session(
                transaction.mapper,
                transaction.inspection_mount_point
            );
            std::error_code error;
            static_cast<void>(fs::remove(transaction.inspection_mount_point, error));
            transaction.cleanup_result = error ? "inspection-cleanup-failed" : "inspection-cleaned";
            if (!error) {
                transaction.mapper.clear();
                transaction.inspection_mount_point.clear();
            }
        } catch (...) {
            transaction.cleanup_result = "inspection-cleanup-failed";
        }
        transaction.updated_at = system_time_seconds();
        transactions_.save(transaction);
        return;
    }
    try {
        cryptsetup_.close(transaction.mapper);
        transaction.cleanup_result = "mapper-closed";
        transaction.mapper.clear();
    } catch (...) {
        transaction.cleanup_result = "mapper-close-failed";
    }
    transaction.updated_at = system_time_seconds();
    transactions_.save(transaction);
}

} // namespace btrfsbackup::daemon::control
