// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <daemon/provisioning/StorageTopology.hpp>

namespace btrfsbackup::daemon::provisioning {

struct ExistingTargetInspectionSummary {
    std::string luks_uuid;
    std::string btrfs_uuid;
    std::string partition_uuid;
    std::string repository_id;
    std::uint64_t catalog_generation = 0;
    std::size_t snapshot_count = 0;

    bool operator==(const ExistingTargetInspectionSummary&) const = default;
};

struct ExistingTargetInspection {
    std::string inspection_id;
    TopologyGeneration topology_generation;
    DeviceCandidateId device_id;
    PartitionCandidateId partition_id;
    ExistingTargetInspectionSummary target;

    bool operator==(const ExistingTargetInspection&) const = default;
};

} // namespace btrfsbackup::daemon::provisioning
