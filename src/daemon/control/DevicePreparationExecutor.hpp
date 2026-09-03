// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <string>

#include <daemon/control/CredentialAdministrationService.hpp>
#include <daemon/control/DevicePreparationPlanBuilder.hpp>
#include <daemon/control/DevicePreparationTransaction.hpp>

namespace btrfsbackup::backup {
class IBtrfsOperations;
class IMountInspector;
} // namespace btrfsbackup::backup

namespace btrfsbackup::config {
class IConfigurationActivator;
}

namespace btrfsbackup::platform::linux::storage {
class IBlockDeviceMetadataReader;
class IBtrfsFilesystemFormatter;
class ICryptsetupOperations;
class IPartitionTableOperations;
class ISignatureOperations;
} // namespace btrfsbackup::platform::linux::storage

namespace btrfsbackup::daemon::control {

class IDestructiveDeviceSafetyInspector;
class IExistingTargetInspector;
class ProvisioningDeviceEnumerator;

class DevicePreparationExecutor final {
  public:
    DevicePreparationExecutor(
        CredentialAdministrationRoots roots,
        std::filesystem::path target_mount_root,
        platform::linux::storage::IBtrfsFilesystemFormatter& btrfs_formatter,
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
        backup::IMountInspector& source_mounts,
        IExistingTargetInspector* existing_target_inspector = nullptr,
        std::filesystem::path inspection_mount_root = {}
    );

    void execute(const std::string& operation_id, int passphrase_fd);
    void recover(const std::string& operation_id);

  private:
    using TransactionMutator = std::function<void(DevicePreparationTransaction&)>;

    void update(const std::string& operation_id, const TransactionMutator& mutator);
    void phase(const std::string& operation_id, const std::string& value, bool can_cancel);
    void completed(const std::string& operation_id, const std::string& value);
    void release_profile_reservation(const std::string& operation_id);
    void release_profile_reservation_after_safe_failure(const std::string& operation_id) noexcept;

    CredentialAdministrationRoots roots_;
    platform::linux::storage::IBtrfsFilesystemFormatter& btrfs_formatter_;
    platform::linux::storage::ISignatureOperations& signatures_;
    platform::linux::storage::IBlockDeviceMetadataReader& metadata_;
    platform::linux::storage::IPartitionTableOperations& partition_tables_;
    platform::linux::storage::ICryptsetupOperations& cryptsetup_;
    backup::IBtrfsOperations& btrfs_;
    config::IConfigurationActivator& activator_;
    ICredentialAdministrationBackend& credentials_;
    IDestructiveDeviceSafetyInspector& safety_inspector_;
    DevicePreparationTransactionStore& transactions_;
    ProvisioningDeviceEnumerator& devices_;
    backup::IMountInspector& source_mounts_;
    DevicePreparationPlanBuilder plan_builder_;
    IExistingTargetInspector* existing_target_inspector_;
    std::filesystem::path inspection_mount_root_;
};

} // namespace btrfsbackup::daemon::control
