// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace btrfsbackup::daemon::provisioning {

using TopologyGeneration = std::string;
using DeviceCandidateId = std::string;
using PartitionCandidateId = std::string;
using UnallocatedRegionId = std::string;

enum class PartitionTableType {
    None,
    Gpt,
    Mbr,
    Unsupported,
};

struct SafetyBlocker {
    std::string code;
    std::string detail;

    bool operator==(const SafetyBlocker&) const = default;
};

struct StableBlockDeviceIdentity {
    std::string display_path;
    std::string major_minor;
    std::string sysfs_path;
    std::string wwn;
    std::string serial;
    std::string serial_short;
    std::uint64_t size_bytes = 0;

    bool operator==(const StableBlockDeviceIdentity&) const = default;
};

struct PartitionTableDescription {
    PartitionTableType type = PartitionTableType::None;
    std::string identifier;

    bool operator==(const PartitionTableDescription&) const = default;
};

struct FilesystemDescription {
    std::string type;
    std::string version;
    std::string label;
    std::string uuid;

    bool operator==(const FilesystemDescription&) const = default;
};

struct ExistingPartition {
    PartitionCandidateId candidate_id;
    StableBlockDeviceIdentity identity;
    std::optional<std::string> partition_uuid;
    std::optional<std::string> partition_label;
    std::uint32_t partition_number = 0;
    std::uint64_t start_sector = 0;
    std::uint64_t sector_count = 0;
    FilesystemDescription filesystem;
    std::vector<std::string> mount_points;
    std::vector<std::string> holders;
    bool active_swap = false;
    std::vector<SafetyBlocker> blockers;
    bool configured_backup_target = false;
    bool suitable_for_reformat = false;
    bool suitable_for_adoption = false;

    bool operator==(const ExistingPartition&) const = default;
};

struct UnallocatedRegion {
    UnallocatedRegionId id;
    std::uint64_t start_sector = 0;
    std::uint64_t sector_count = 0;
    std::vector<SafetyBlocker> blockers;
    bool suitable_for_backup_partition = false;

    bool operator==(const UnallocatedRegion&) const = default;
};

using StorageRegion = std::variant<ExistingPartition, UnallocatedRegion>;

struct StorageDevice {
    DeviceCandidateId candidate_id;
    StableBlockDeviceIdentity identity;
    std::string display_name;
    std::string transport;
    std::uint64_t size_bytes = 0;
    std::uint32_t logical_sector_size = 0;
    std::uint32_t physical_sector_size = 0;
    bool removable = false;
    bool read_only = false;
    bool hotplug = false;
    PartitionTableDescription partition_table;
    FilesystemDescription filesystem;
    std::vector<StorageRegion> regions;
    std::vector<std::string> mount_points;
    std::vector<std::string> holders;
    bool active_swap = false;
    std::vector<SafetyBlocker> blockers;

    bool operator==(const StorageDevice&) const = default;
};

struct StorageTopology {
    TopologyGeneration generation;
    std::vector<StorageDevice> devices;

    bool operator==(const StorageTopology&) const = default;
};

[[nodiscard]] std::string partition_table_type_name(PartitionTableType type);
[[nodiscard]] std::uint64_t region_start_sector(const StorageRegion& region);

} // namespace btrfsbackup::daemon::provisioning
