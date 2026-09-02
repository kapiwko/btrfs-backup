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
class ICommandRunner;
} // namespace btrfsbackup::backup

namespace btrfsbackup::config {
class IConfigurationActivator;
}

namespace btrfsbackup::platform::linux::storage {
class ISignatureOperations;
}

namespace btrfsbackup::daemon::control {

class IDestructiveDeviceSafetyInspector;
class ProvisioningDeviceEnumerator;

class DevicePreparationExecutor final {
  public:
    DevicePreparationExecutor(
        CredentialAdministrationRoots roots,
        std::filesystem::path target_mount_root,
        backup::ICommandRunner& commands,
        platform::linux::storage::ISignatureOperations& signatures,
        backup::IBtrfsOperations& btrfs,
        config::IConfigurationActivator& configuration_activator,
        ICredentialAdministrationBackend& credentials,
        IDestructiveDeviceSafetyInspector& safety_inspector,
        DevicePreparationTransactionStore& transactions,
        ProvisioningDeviceEnumerator& devices
    );

    void execute(const std::string& operation_id, int passphrase_fd);
    void recover(const std::string& operation_id);

  private:
    using TransactionMutator = std::function<void(DevicePreparationTransaction&)>;

    void update(const std::string& operation_id, const TransactionMutator& mutator);
    void phase(const std::string& operation_id, const std::string& value, bool can_cancel);
    void completed(const std::string& operation_id, const std::string& value);

    CredentialAdministrationRoots roots_;
    backup::ICommandRunner& commands_;
    platform::linux::storage::ISignatureOperations& signatures_;
    backup::IBtrfsOperations& btrfs_;
    config::IConfigurationActivator& activator_;
    ICredentialAdministrationBackend& credentials_;
    IDestructiveDeviceSafetyInspector& safety_inspector_;
    DevicePreparationTransactionStore& transactions_;
    ProvisioningDeviceEnumerator& devices_;
    DevicePreparationPlanBuilder plan_builder_;
};

} // namespace btrfsbackup::daemon::control
