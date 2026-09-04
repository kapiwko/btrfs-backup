// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/DevicePreparationExecutor.hpp>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <limits>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <thread>
#include <utility>
#include <unistd.h>

#include <backup/ports/IBtrfsOperations.hpp>
#include <backup/ports/IMountInspector.hpp>
#include <config/ports/ConfigurationActivator.hpp>
#include <core/Errors.hpp>
#include <core/Identifiers.hpp>
#include <daemon/control/DestructiveDeviceSafetyInspector.hpp>
#include <daemon/control/ExistingTargetInspector.hpp>
#include <daemon/control/ProvisioningDeviceEnumerator.hpp>
#include <daemon/control/ProvisioningSource.hpp>
#include <daemon/dbus/ManagerErrors.hpp>
#include <platform/linux/config/ProfileService.hpp>
#include <platform/linux/filesystem/FileLock.hpp>
#include <platform/linux/filesystem/SecretFile.hpp>
#include <platform/linux/filesystem/TrustedDirectory.hpp>
#include <platform/linux/storage/BlockDeviceMetadata.hpp>
#include <platform/linux/storage/provisioning/BtrfsFilesystemFormatter.hpp>
#include <platform/linux/storage/CryptsetupOperations.hpp>
#include <platform/linux/storage/provisioning/PartitionTableOperations.hpp>
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

void rewind_secret(int fd) {
    if (::lseek(fd, 0, SEEK_SET) < 0)
        throw ValidationError("cannot rewind device preparation secret");
}

std::string block_device_number(const fs::path& path) {
    struct stat status{};
    if (::stat(path.c_str(), &status) < 0 || !S_ISBLK(status.st_mode))
        throw ValidationError("device preparation access target is not a block device");
    return std::to_string(::major(status.st_rdev)) + ":" + std::to_string(::minor(status.st_rdev));
}

void remove_inspection_mount_point(const fs::path& mount_point) {
    std::error_code error;
    if (!fs::remove(mount_point, error) || error)
        throw ValidationError("cannot remove existing target inspection mount point");
}

platform::linux::storage::provisioning::PartitionTableFormat partition_table_format(
    provisioning::PartitionTableType type
) {
    switch (type) {
    case provisioning::PartitionTableType::None:
        return platform::linux::storage::provisioning::PartitionTableFormat::None;
    case provisioning::PartitionTableType::Gpt:
        return platform::linux::storage::provisioning::PartitionTableFormat::Gpt;
    case provisioning::PartitionTableType::Mbr:
        return platform::linux::storage::provisioning::PartitionTableFormat::Mbr;
    case provisioning::PartitionTableType::Unsupported:
        throw ValidationError("unsupported partition table cannot be backed up safely");
    }
    throw ValidationError("partition table type is invalid");
}

} // namespace

DevicePreparationExecutor::DevicePreparationExecutor(
    CredentialAdministrationRoots roots,
    fs::path target_mount_root,
    platform::linux::storage::provisioning::IBtrfsFilesystemFormatter& btrfs_formatter,
    platform::linux::storage::ISignatureOperations& signatures,
    platform::linux::storage::IBlockDeviceMetadataReader& metadata,
    platform::linux::storage::provisioning::IPartitionTableOperations& partition_tables,
    platform::linux::storage::ICryptsetupOperations& cryptsetup,
    backup::IBtrfsOperations& btrfs,
    config::IConfigurationActivator& configuration_activator,
    ICredentialAdministrationBackend& credentials,
    IDestructiveDeviceSafetyInspector& safety_inspector,
    DevicePreparationTransactionStore& transactions,
    ProvisioningDeviceEnumerator& devices,
    backup::IMountInspector& source_mounts,
    IExistingTargetInspector* existing_target_inspector,
    fs::path inspection_mount_root,
    BlockDeviceNumberResolver block_device_number_resolver
)
    : roots_(std::move(roots)),
      btrfs_formatter_(btrfs_formatter),
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
      source_mounts_(source_mounts),
      profile_builder_(std::move(target_mount_root)),
      existing_target_inspector_(existing_target_inspector),
      inspection_mount_root_(std::move(inspection_mount_root)),
      recovery_(transactions, partition_tables, cryptsetup, existing_target_inspector),
      block_device_number_resolver_(block_device_number_resolver ? std::move(block_device_number_resolver) : BlockDeviceNumberResolver{block_device_number}) {
}

void DevicePreparationExecutor::update(
    const std::string& operation_id,
    const TransactionMutator& mutator
) {
    static_cast<void>(transactions_.update(operation_id, [&](auto& transaction) {
        mutator(transaction);
        transaction.updated_at = system_time_seconds();
    }));
}

void DevicePreparationExecutor::phase(
    const std::string& operation_id,
    const std::string& value,
    bool can_cancel
) {
    update(operation_id, [&](auto& transaction) {
        if (transaction.cancel_requested)
            throw ValidationError("device preparation cancellation was requested");
        transaction.status.state = "running";
        transaction.status.phase = value;
        transaction.status.can_cancel = can_cancel;
    });
}

void DevicePreparationExecutor::completed(const std::string& operation_id, const std::string& value) {
    update(operation_id, [&](auto& transaction) { transaction.last_completed_phase = value; });
}

void DevicePreparationExecutor::request_access(
    const std::string& operation_id,
    std::vector<std::string> devices,
    bool mapper_control
) {
    std::ranges::sort(devices);
    devices.erase(std::unique(devices.begin(), devices.end()), devices.end());
    std::uint64_t generation = 0;
    update(operation_id, [&](auto& transaction) {
        transaction.requested_device_access = std::move(devices);
        transaction.requested_mapper_control = mapper_control;
        if (transaction.access_generation == std::numeric_limits<std::uint64_t>::max())
            throw ValidationError("device preparation access generation is exhausted");
        generation = ++transaction.access_generation;
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto transaction = transactions_.load(operation_id);
        if (transaction.authorized_access_generation >= generation)
            return;
        if (transaction.cancel_requested)
            throw ValidationError("device preparation cancellation was requested");
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    throw ValidationError("device preparation access authorization timed out");
}

void DevicePreparationExecutor::release_profile_reservation(const std::string& operation_id) {
    DevicePreparationTransaction transaction = transactions_.load(operation_id);
    if (transaction.profile_reservation_state != "held")
        return;
    transaction.profile_reservation_state = "releasing";
    transaction.updated_at = system_time_seconds();
    transactions_.save(transaction);
    transactions_.release_profile(transaction.status.profile_id, operation_id);
    transaction.profile_reservation_state = "released";
    transaction.updated_at = system_time_seconds();
    transactions_.save(transaction);
}

void DevicePreparationExecutor::release_profile_reservation_after_safe_failure(
    const std::string& operation_id
) noexcept {
    try {
        const DevicePreparationTransaction transaction = transactions_.load(operation_id);
        const bool profile_committed = transaction.configuration_state == "installed";
        const bool target_unchanged =
            transaction.target.mode == provisioning::ProvisioningMode::AdoptExistingTarget ||
            transaction.last_completed_phase.empty() || transaction.last_completed_phase == "inspect" ||
            transaction.last_completed_phase == "backup-partition-table";
        if (profile_committed || target_unchanged)
            release_profile_reservation(operation_id);
    } catch (...) {
    }
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
            if (!initial.target.planned_partition_geometry.has_value())
                throw ValidationError("whole-device preparation transaction is incomplete");
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
        const SourceCandidate source = resolve_provisioning_source(
            source_mounts_.inspect(),
            initial.source_subvolume,
            ProfileId{initial.status.profile_id}
        );
        if (source.filesystem_uuid != initial.source_filesystem_uuid ||
            source.mount_root != initial.source_mount_root ||
            source.local_snapshot_root != initial.local_snapshot_dir)
            throw ValidationError("selected source filesystem changed before device preparation");
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
            const std::string partition_device_number = initial.target.partition->identity.major_minor;
            try {
                request_access(
                    operation_id,
                    {initial.device.major_minor, partition_device_number},
                    true
                );
                rewind_secret(passphrase.get());
                inspected = existing_target_inspector_->inspect(
                    *initial.target.partition,
                    operation_id,
                    mount_point,
                    passphrase.get(),
                    [&](const fs::path& mapper_path) {
                        const std::string mapper_device_number = block_device_number_resolver_(mapper_path);
                        update(operation_id, [&](auto& transaction) {
                            transaction.mapper_device_number = mapper_device_number;
                        });
                        request_access(
                            operation_id,
                            {initial.device.major_minor, partition_device_number, mapper_device_number},
                            true
                        );
                    }
                );
                remove_inspection_mount_point(mount_point);
                update(operation_id, [](auto& transaction) {
                    transaction.mapper.clear();
                    transaction.mapper_device_number.clear();
                    transaction.inspection_mount_point.clear();
                    transaction.cleanup_result = "inspection-cleaned";
                });
            } catch (...) {
                std::error_code cleanup_error;
                static_cast<void>(fs::remove(mount_point, cleanup_error));
                update(operation_id, [&](auto& transaction) {
                    transaction.mapper.clear();
                    transaction.mapper_device_number.clear();
                    transaction.inspection_mount_point.clear();
                    transaction.cleanup_result = cleanup_error ? "inspection-directory-remove-failed" : "inspection-cleaned";
                });
                throw;
            }
            request_access(
                operation_id,
                {initial.device.major_minor, partition_device_number},
                false
            );
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
            config::Profile profile = profile_builder_.build(
                initial,
                inspected.luks_uuid,
                inspected.btrfs_uuid,
                inspected.partition_uuid
            );
            profile.enabled = false;
            const platform::linux::config::ExpectedProfileIdentity expected_profile{
                .exists = false,
                .generation = {},
                .fingerprint = {},
            };
            platform::linux::config::install_profile(
                profile,
                {roots_.config_root, roots_.udev_root, roots_.systemd_root, roots_.public_root},
                activator_,
                &expected_profile
            );
            update(operation_id, [](auto& transaction) {
                transaction.configuration_state = "installed";
                transaction.credentials_state = "not-applicable";
                transaction.last_completed_phase = "write-profile";
            });
            release_profile_reservation(operation_id);
            update(operation_id, [](auto& transaction) {
                transaction.status.state = "succeeded";
                transaction.status.phase = "complete";
                transaction.status.can_cancel = false;
                transaction.status.recovery_action.clear();
            });
            return;
        }
        std::string partition;
        if (initial.target.mode == provisioning::ProvisioningMode::EraseWholeDevice) {
            phase(operation_id, "backup-partition-table", false);
            const std::string partition_table_backup = partition_tables_.snapshot_partition_table(
                initial.device.path,
                initial.device.major_minor,
                partition_table_format(initial.target.device.partition_table.type),
                initial.target.device.partition_table.identifier,
                initial.target.device.logical_sector_size
            );
            if (partition_table_backup.empty())
                throw ValidationError("partition table backup is empty");
            update(operation_id, [&](auto& transaction) {
                transaction.partition_table_backup = partition_table_backup;
                transaction.last_completed_phase = "backup-partition-table";
            });

            phase(operation_id, "wipe-signatures", false);
            signatures_.wipe_all(initial.device.path, initial.device.major_minor);
            completed(operation_id, "wipe-signatures");

            phase(operation_id, "partition", false);
            const auto& planned = *initial.target.planned_partition_geometry;
            partition = partition_tables_
                            .replace_with_single_gpt_partition(
                                initial.device.path,
                                initial.device.major_minor,
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
        } else if (initial.target.mode == provisioning::ProvisioningMode::CreatePartitionInUnallocatedSpace) {
            phase(operation_id, "backup-partition-table", false);
            const std::string partition_table_backup = partition_tables_.snapshot_partition_table(
                initial.device.path,
                initial.device.major_minor,
                partition_table_format(initial.target.device.partition_table.type),
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

        std::string partition_device_number = initial.target.partition.has_value()
            ? initial.target.partition->identity.major_minor
            : block_device_number_resolver_(partition);
        if (!initial.target.partition.has_value()) {
            update(operation_id, [&](auto& transaction) {
                transaction.partition_device_number = partition_device_number;
            });
            request_access(
                operation_id,
                {initial.device.major_minor, partition_device_number},
                false
            );
        }

        phase(operation_id, "luks-format", false);
        const std::string luks_uuid = cryptsetup_.format_luks2(partition, passphrase.get());
        update(operation_id, [&](auto& transaction) {
            transaction.luks_uuid = luks_uuid;
            transaction.last_completed_phase = "luks-format";
        });

        phase(operation_id, "open", false);
        request_access(
            operation_id,
            {initial.device.major_minor, partition_device_number},
            true
        );
        rewind_secret(passphrase.get());
        mapper = "btrfs-backup-" + initial.status.profile_id;
        cryptsetup_.open_luks2(partition, mapper, passphrase.get());
        update(operation_id, [&](auto& transaction) {
            transaction.mapper = mapper;
            transaction.last_completed_phase = "open";
            transaction.cleanup_result = "pending";
        });
        const std::string mapper_path = "/dev/mapper/" + mapper;
        const std::string mapper_device_number = block_device_number_resolver_(mapper_path);
        update(operation_id, [&](auto& transaction) {
            transaction.mapper_device_number = mapper_device_number;
        });
        request_access(
            operation_id,
            {initial.device.major_minor, partition_device_number, mapper_device_number},
            true
        );

        phase(operation_id, "mkfs-btrfs", false);
        btrfs_formatter_.format(mapper_path, initial.profile_name);
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
            transaction.mapper_device_number.clear();
            transaction.cleanup_result = "mapper-closed";
            transaction.last_completed_phase = "close";
        });
        request_access(
            operation_id,
            {initial.device.major_minor, partition_device_number},
            false
        );

        phase(operation_id, "write-profile", false);
        update(operation_id, [](auto& transaction) { transaction.configuration_state = "in-progress"; });
        config::Profile profile = profile_builder_.build(initial, luks_uuid, btrfs_uuid, partition_uuid);
        const platform::linux::config::ExpectedProfileIdentity expected_profile{
            .exists = false,
            .generation = {},
            .fingerprint = {},
        };
        platform::linux::config::install_profile(
            profile,
            {roots_.config_root, roots_.udev_root, roots_.systemd_root, roots_.public_root},
            activator_,
            &expected_profile
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
        });
        release_profile_reservation(operation_id);
        update(operation_id, [](auto& transaction) {
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
                transaction.status.state = transaction.cancel_requested ? "cancelled" : "failed";
                transaction.status.error_code = transaction.cancel_requested
                    ? std::string{}
                    : "device-preparation." + transaction.status.phase + "-failed";
                transaction.status.recovery_action = transaction.cancel_requested
                    ? std::string{}
                    : (initial.target.mode == provisioning::ProvisioningMode::AdoptExistingTarget
                           ? "Rescan and inspect the existing target before creating a new adoption plan."
                           : "Inspect the recorded device artifacts and complete or remove partial structures manually.");
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
        release_profile_reservation_after_safe_failure(operation_id);
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
                transaction.status.state = transaction.cancel_requested ? "cancelled" : "failed";
                transaction.status.error_code = transaction.cancel_requested
                    ? std::string{}
                    : "device-preparation.unknown-failed";
                transaction.status.recovery_action = transaction.cancel_requested
                    ? std::string{}
                    : (initial.target.mode == provisioning::ProvisioningMode::AdoptExistingTarget
                           ? "Rescan and inspect the existing target before creating a new adoption plan."
                           : "Inspect and repair the recorded device artifacts manually.");
                transaction.status.can_cancel = false;
                transaction.cleanup_result = !cleanup_required
                    ? "not-required"
                    : (cleanup_ok ? "mapper-closed" : "mapper-close-failed");
                if (cleanup_ok)
                    transaction.mapper.clear();
            });
        } catch (...) {
        }
        release_profile_reservation_after_safe_failure(operation_id);
    }
}

void DevicePreparationExecutor::recover(const std::string& operation_id) {
    recovery_.recover(operation_id);
}

} // namespace btrfsbackup::daemon::control
