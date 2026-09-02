// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <memory>

#include <daemon/control/DeviceProvisioningService.hpp>

namespace btrfsbackup::backup {
class IBtrfsOperations;
class ICommandRunner;
} // namespace btrfsbackup::backup

namespace btrfsbackup::config {
class IConfigurationActivator;
}

namespace btrfsbackup::platform::linux::storage {
class IBlockDeviceMetadataReader;
class ICryptsetupOperations;
class IPartitionTableOperations;
class ISignatureOperations;
} // namespace btrfsbackup::platform::linux::storage

namespace btrfsbackup::daemon::provisioning {
class StorageTopologyReader;
}

namespace btrfsbackup::daemon::control {

class ICredentialAdministrationBackend;
class IDestructiveDeviceSafetyInspector;
class IDevicePreparationUnitController;
class IExistingTargetInspector;
struct CredentialAdministrationRoots;

class SystemDeviceProvisioningBackend final : public IDeviceProvisioningBackend {
  public:
    SystemDeviceProvisioningBackend(
        CredentialAdministrationRoots roots,
        std::filesystem::path target_mount_root,
        std::filesystem::path mountinfo_path,
        std::filesystem::path transaction_root,
        provisioning::StorageTopologyReader& topology,
        btrfsbackup::backup::ICommandRunner& commands,
        btrfsbackup::platform::linux::storage::ISignatureOperations& signatures,
        btrfsbackup::platform::linux::storage::IBlockDeviceMetadataReader& metadata,
        btrfsbackup::platform::linux::storage::IPartitionTableOperations& partition_tables,
        btrfsbackup::platform::linux::storage::ICryptsetupOperations& cryptsetup,
        btrfsbackup::backup::IBtrfsOperations& btrfs,
        btrfsbackup::config::IConfigurationActivator& configuration_activator,
        ICredentialAdministrationBackend& credentials,
        IDestructiveDeviceSafetyInspector& safety_inspector,
        IDevicePreparationUnitController& units,
        bool recover_existing = true,
        IExistingTargetInspector* existing_target_inspector = nullptr,
        std::filesystem::path inspection_mount_root = {}
    );
    ~SystemDeviceProvisioningBackend() noexcept override;

    [[nodiscard]] std::vector<std::string> list_source_candidates() override;
    [[nodiscard]] std::vector<std::string> inspect_safety(
        const DevicePreparationTarget& target
    ) const override;
    [[nodiscard]] provisioning::PlannedPartitionGeometry plan_partition_geometry(
        const provisioning::StorageDevice& device,
        const provisioning::UnallocatedRegion& free_region
    ) const override;
    [[nodiscard]] provisioning::ExistingTargetInspectionSummary inspect_existing_target(
        const DevicePreparationTarget& target,
        int credential_fd
    ) override;
    [[nodiscard]] DevicePreparationStatus start(
        const DevicePreparationRequest& request,
        const DevicePreparationTarget& target,
        const DevicePreparationOwner& owner,
        int passphrase_fd
    ) override;
    [[nodiscard]] DevicePreparationStatus status(const std::string& operation_id) const override;
    [[nodiscard]] bool owned_by(
        const std::string& operation_id,
        const DevicePreparationOwner& owner
    ) const override;
    void cancel(const std::string& operation_id) override;
    void execute_operation(const std::string& operation_id, int passphrase_fd);
    void recover_operation(const std::string& operation_id);

  private:
    struct State;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace btrfsbackup::daemon::control
