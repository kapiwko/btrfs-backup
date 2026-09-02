// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <daemon/provisioning/DevicePreparationPlan.hpp>

namespace btrfsbackup::daemon::provisioning {

class DevicePreparationPlanBuilder final {
  public:
    [[nodiscard]] DevicePreparationPlan build(
        const StorageTopology& topology,
        const TopologyGeneration& expected_generation,
        const std::string& selected_candidate_id,
        ProvisioningMode mode,
        DevicePreparationPlanId plan_id,
        std::optional<std::string> inspection_id = std::nullopt,
        std::optional<PlannedPartitionGeometry> partition_geometry = std::nullopt
    ) const;
};

} // namespace btrfsbackup::daemon::provisioning
