// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include <daemon/control/DeviceProvisioningService.hpp>

namespace btrfsbackup::backup {
class ICommandRunner;
}

namespace btrfsbackup::daemon::control {

class ProvisioningDeviceEnumerator final {
  public:
    explicit ProvisioningDeviceEnumerator(backup::ICommandRunner& commands);

    [[nodiscard]] std::vector<ProvisioningDevice> list();
    [[nodiscard]] ProvisioningDevice revalidate(const ProvisioningDevice& expected);

  private:
    backup::ICommandRunner& commands_;
};

} // namespace btrfsbackup::daemon::control
