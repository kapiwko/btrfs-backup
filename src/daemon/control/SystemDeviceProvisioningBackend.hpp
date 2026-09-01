// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <filesystem>
#include <memory>

#include <backup/ports/ICommandRunner.hpp>
#include <backup/ports/IBtrfsOperations.hpp>
#include <config/ports/ConfigurationActivator.hpp>
#include <daemon/control/DeviceProvisioningService.hpp>
#include <daemon/control/CredentialAdministrationService.hpp>

namespace btrfsbackup::daemon::control {

class SystemDeviceProvisioningBackend final : public IDeviceProvisioningBackend {
  public:
    SystemDeviceProvisioningBackend(
        CredentialAdministrationRoots roots,
        std::filesystem::path target_mount_root,
        std::filesystem::path mountinfo_path,
        btrfsbackup::backup::ICommandRunner& commands,
        btrfsbackup::backup::IBtrfsOperations& btrfs,
        btrfsbackup::config::IConfigurationActivator& configuration_activator,
        ICredentialAdministrationBackend& credentials
    );
    ~SystemDeviceProvisioningBackend() override;

    [[nodiscard]] std::vector<ProvisioningDevice> list_devices() override;
    [[nodiscard]] std::vector<std::string> list_source_candidates() override;
    [[nodiscard]] DevicePreparationStatus start(const DevicePreparationRequest& request, int passphrase_fd) override;
    [[nodiscard]] DevicePreparationStatus status(const std::string& operation_id) const override;
    void cancel(const std::string& operation_id) override;

  private:
    struct State;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace btrfsbackup::daemon::control
