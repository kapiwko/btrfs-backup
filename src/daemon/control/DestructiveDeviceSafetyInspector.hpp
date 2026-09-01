// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <daemon/control/DeviceProvisioningService.hpp>

namespace btrfsbackup::daemon::provisioning {
class StorageTopologyReader;
}

namespace btrfsbackup::daemon::control {

class IDestructiveDeviceSafetyInspector {
  public:
    virtual ~IDestructiveDeviceSafetyInspector() = default;
    [[nodiscard]] virtual std::vector<std::string> inspect(
        const ProvisioningDevice& expected_device
    ) const = 0;
};

using ExclusiveDeviceProbe = std::function<std::optional<std::string>(const ProvisioningDevice&)>;

class DestructiveDeviceSafetyInspector final : public IDestructiveDeviceSafetyInspector {
  public:
    explicit DestructiveDeviceSafetyInspector(
        provisioning::StorageTopologyReader& topology,
        ExclusiveDeviceProbe exclusive_probe = {}
    );

    [[nodiscard]] std::vector<std::string> inspect(
        const ProvisioningDevice& expected_device
    ) const override;

  private:
    provisioning::StorageTopologyReader& topology_;
    ExclusiveDeviceProbe exclusive_probe_;
};

} // namespace btrfsbackup::daemon::control
