// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <provisioning/StorageTopology.hpp>

namespace btrfsbackup::provisioning {

using DevicePreparationPlanId = std::string;

enum class ProvisioningMode {
    EraseWholeDevice,
    ReformatExistingPartition,
    CreatePartitionInUnallocatedSpace,
    AdoptExistingTarget,
};

enum class PredictedRegionKind {
    ExistingPartition,
    Unallocated,
    BackupPartition,
};

struct PredictedStorageRegion {
    std::string id;
    PredictedRegionKind kind = PredictedRegionKind::Unallocated;
    std::uint64_t start_sector = 0;
    std::uint64_t sector_count = 0;
    std::optional<std::uint32_t> partition_number;
    std::string display_path;
    std::string partition_label;
    std::string filesystem_type;
    bool geometry_exact = true;
    bool encrypted = false;
    bool changed = false;
    bool data_will_be_erased = false;

    bool operator==(const PredictedStorageRegion&) const = default;
};

struct StorageLayout {
    DeviceCandidateId device_id;
    std::uint64_t size_bytes = 0;
    std::uint32_t logical_sector_size = 0;
    PartitionTableType partition_table_type = PartitionTableType::None;
    std::vector<PredictedStorageRegion> regions;

    bool operator==(const StorageLayout&) const = default;
};

struct EraseDeviceSignatures {
    DeviceCandidateId device_id;
    bool operator==(const EraseDeviceSignatures&) const = default;
};

struct BackupPartitionTable {
    DeviceCandidateId device_id;
    bool operator==(const BackupPartitionTable&) const = default;
};

struct ErasePartitionSignatures {
    PartitionCandidateId partition_id;
    bool operator==(const ErasePartitionSignatures&) const = default;
};

struct CreateGptPartitionTable {
    DeviceCandidateId device_id;
    bool operator==(const CreateGptPartitionTable&) const = default;
};

struct PlannedPartitionGeometry {
    std::uint64_t start_sector = 0;
    std::uint64_t sector_count = 0;
    std::uint32_t partition_number = 0;
    bool operator==(const PlannedPartitionGeometry&) const = default;
};

struct CreateBackupPartition {
    DeviceCandidateId device_id;
    std::optional<UnallocatedRegionId> free_region_id;
    std::optional<PlannedPartitionGeometry> geometry;
    bool operator==(const CreateBackupPartition&) const = default;
};

struct FormatLuks2 {
    std::optional<PartitionCandidateId> partition_id;
    bool operator==(const FormatLuks2&) const = default;
};

struct OpenLuksMapping {
    bool operator==(const OpenLuksMapping&) const = default;
};

struct FormatBtrfs {
    bool operator==(const FormatBtrfs&) const = default;
};

struct VerifyPreparedTarget {
    bool operator==(const VerifyPreparedTarget&) const = default;
};

struct PublishProfile {
    bool operator==(const PublishProfile&) const = default;
};

using PlannedStorageOperation = std::variant<
    BackupPartitionTable,
    EraseDeviceSignatures,
    ErasePartitionSignatures,
    CreateGptPartitionTable,
    CreateBackupPartition,
    FormatLuks2,
    OpenLuksMapping,
    FormatBtrfs,
    VerifyPreparedTarget,
    PublishProfile>;

struct SafetyWarning {
    std::string code;
    std::string detail;
    bool operator==(const SafetyWarning&) const = default;
};

enum class DestructiveScopeKind {
    None,
    WholeDevice,
    ExistingPartition,
    UnallocatedRegion,
};

class DestructiveScope {
  public:
    [[nodiscard]] static DestructiveScope whole_device(DeviceCandidateId device_id);
    [[nodiscard]] static DestructiveScope existing_partition(
        DeviceCandidateId device_id,
        PartitionCandidateId partition_id
    );
    [[nodiscard]] static DestructiveScope unallocated_region(
        DeviceCandidateId device_id,
        UnallocatedRegionId free_region_id
    );
    [[nodiscard]] static DestructiveScope adoption(
        DeviceCandidateId device_id,
        PartitionCandidateId partition_id
    );

    [[nodiscard]] DestructiveScopeKind kind() const noexcept;
    [[nodiscard]] const DeviceCandidateId& device_id() const noexcept;
    [[nodiscard]] const std::optional<PartitionCandidateId>& partition_id() const noexcept;
    [[nodiscard]] const std::optional<UnallocatedRegionId>& free_region_id() const noexcept;

    bool operator==(const DestructiveScope&) const = default;

  private:
    DestructiveScope(
        DestructiveScopeKind kind,
        DeviceCandidateId device_id,
        std::optional<PartitionCandidateId> partition_id,
        std::optional<UnallocatedRegionId> free_region_id
    );

    DestructiveScopeKind kind_;
    DeviceCandidateId device_id_;
    std::optional<PartitionCandidateId> partition_id_;
    std::optional<UnallocatedRegionId> free_region_id_;
};

struct DevicePreparationPlan {
    DevicePreparationPlanId id;
    TopologyGeneration topology_generation;
    ProvisioningMode mode = ProvisioningMode::EraseWholeDevice;
    DeviceCandidateId device_id;
    std::optional<PartitionCandidateId> partition_id;
    std::optional<UnallocatedRegionId> free_region_id;
    std::optional<std::string> inspection_id;
    StorageLayout before;
    StorageLayout after;
    std::vector<PlannedStorageOperation> operations;
    std::vector<SafetyWarning> warnings;
    DestructiveScope destructive_scope;

    bool operator==(const DevicePreparationPlan&) const = default;

    void validate() const;
};

[[nodiscard]] std::string provisioning_mode_name(ProvisioningMode mode);
[[nodiscard]] std::optional<ProvisioningMode> provisioning_mode_from_name(std::string_view name);
[[nodiscard]] std::string predicted_region_kind_name(PredictedRegionKind kind);

} // namespace btrfsbackup::provisioning
