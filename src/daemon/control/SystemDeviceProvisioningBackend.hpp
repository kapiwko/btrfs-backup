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

namespace btrfsbackup::daemon::provisioning {
class StorageTopologyReader;
}

namespace btrfsbackup::daemon::control {

class ICredentialAdministrationBackend;
class IDestructiveDeviceSafetyInspector;
class IDevicePreparationUnitController;
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
        btrfsbackup::backup::IBtrfsOperations& btrfs,
        btrfsbackup::config::IConfigurationActivator& configuration_activator,
        ICredentialAdministrationBackend& credentials,
        IDestructiveDeviceSafetyInspector& safety_inspector,
        IDevicePreparationUnitController& units,
        bool recover_existing = true
    );
    ~SystemDeviceProvisioningBackend() noexcept override;

    [[nodiscard]] std::vector<ProvisioningDevice> list_devices() override;
    [[nodiscard]] std::vector<std::string> list_source_candidates() override;
    [[nodiscard]] std::vector<std::string> inspect_safety(
        const ProvisioningDevice& expected_device
    ) const override;
    [[nodiscard]] DevicePreparationStatus start(
        const DevicePreparationRequest& request,
        const ProvisioningDevice& expected_device,
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
