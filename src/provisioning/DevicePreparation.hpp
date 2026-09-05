// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <provisioning/DevicePreparationPlan.hpp>
#include <provisioning/ExistingTargetInspection.hpp>

namespace btrfsbackup::provisioning {

struct DevicePreparationSource {
    std::string candidate_id;
    std::string name;
    std::string subvolume;
    std::string filesystem_uuid;
    std::string mount_root;
    std::string local_snapshot_dir;
    std::size_t local_retention = 30;
    std::size_t remote_retention = 30;

    auto operator<=>(const DevicePreparationSource&) const = default;
};

struct ProvisioningDevice {
    std::string path;
    std::string model;
    std::string serial;
    std::string transport;
    std::uint64_t size_bytes = 0;
    bool removable = false;
    bool mounted = false;
    bool contains_data = false;
    std::string major_minor;
    std::string sysfs_devpath;
    std::string wwn;
    std::string serial_id;
    std::string serial_short;
    std::string device_graph;
};

struct DevicePreparationRequest {
    std::string profile_id;
    std::string profile_name;
    std::string plan_id;
    std::string source_candidate_id;
    std::vector<DevicePreparationSource> sources;
    std::string passphrase_label;
    bool create_automatic_key = true;
};

struct DevicePreparationStatus {
    std::string operation_id;
    std::string profile_id;
    std::string state;
    std::string phase;
    std::string error_code;
    std::string recovery_action;
    std::string last_completed_phase;
    std::string cleanup_result = "not-required";
    bool can_cancel = false;
};

struct DevicePreparationOwner {
    std::string bus_name;
    std::uint32_t uid = 0;
};

struct DevicePreparationTarget {
    ProvisioningMode mode = ProvisioningMode::EraseWholeDevice;
    StorageDevice device;
    std::optional<ExistingPartition> partition;
    std::optional<UnallocatedRegion> free_region;
    std::optional<PlannedPartitionGeometry> planned_partition_geometry;
    std::optional<ExistingTargetInspectionSummary> expected_inspection;
};

} // namespace btrfsbackup::provisioning
