// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include <daemon/control/DeviceProvisioningService.hpp>

namespace btrfsbackup::daemon::provisioning {
class StorageTopologyReader;
}

namespace btrfsbackup::daemon::control {

[[nodiscard]] ProvisioningDevice provisioning_device_snapshot(
    const provisioning::StorageDevice& device
);

class ProvisioningDeviceEnumerator final {
  public:
    explicit ProvisioningDeviceEnumerator(provisioning::StorageTopologyReader& topology);

    [[nodiscard]] std::vector<ProvisioningDevice> list();
    [[nodiscard]] ProvisioningDevice revalidate(const ProvisioningDevice& expected);
    [[nodiscard]] std::string only_partition(const ProvisioningDevice& expected_device);

  private:
    provisioning::StorageTopologyReader& topology_;
};

} // namespace btrfsbackup::daemon::control
