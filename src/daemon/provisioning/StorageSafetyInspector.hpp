// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <vector>

#include <daemon/provisioning/DevicePreparationPlan.hpp>

namespace btrfsbackup::daemon::provisioning {

class StorageSafetyInspector final {
  public:
    [[nodiscard]] std::vector<SafetyBlocker> inspect(
        const StorageTopology& expected,
        const StorageTopology& current,
        const DevicePreparationPlan& plan
    ) const;
};

} // namespace btrfsbackup::daemon::provisioning
