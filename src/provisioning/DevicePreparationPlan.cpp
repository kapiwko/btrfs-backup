// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <provisioning/DevicePreparationPlan.hpp>

#include <utility>

#include <core/Errors.hpp>

namespace btrfsbackup::provisioning {

namespace {

void require_identifier(const std::string& value, const char* message) {
    if (value.empty())
        throw ValidationError(message);
}

} // namespace

DestructiveScope::DestructiveScope(
    DestructiveScopeKind kind,
    DeviceCandidateId device_id,
    std::optional<PartitionCandidateId> partition_id,
    std::optional<UnallocatedRegionId> free_region_id
) : kind_(kind), device_id_(std::move(device_id)), partition_id_(std::move(partition_id)),
    free_region_id_(std::move(free_region_id)) {
    require_identifier(device_id_, "destructive scope device identifier is empty");
}

DestructiveScope DestructiveScope::whole_device(DeviceCandidateId device_id) {
    return {DestructiveScopeKind::WholeDevice, std::move(device_id), std::nullopt, std::nullopt};
}

DestructiveScope DestructiveScope::existing_partition(
    DeviceCandidateId device_id,
    PartitionCandidateId partition_id
) {
    require_identifier(partition_id, "destructive scope partition identifier is empty");
    return {
        DestructiveScopeKind::ExistingPartition,
        std::move(device_id),
        std::move(partition_id),
        std::nullopt,
    };
}

DestructiveScope DestructiveScope::unallocated_region(
    DeviceCandidateId device_id,
    UnallocatedRegionId free_region_id
) {
    require_identifier(free_region_id, "destructive scope free-region identifier is empty");
    return {
        DestructiveScopeKind::UnallocatedRegion,
        std::move(device_id),
        std::nullopt,
        std::move(free_region_id),
    };
}

DestructiveScope DestructiveScope::adoption(
    DeviceCandidateId device_id,
    PartitionCandidateId partition_id
) {
    require_identifier(partition_id, "adoption scope partition identifier is empty");
    return {
        DestructiveScopeKind::None,
        std::move(device_id),
        std::move(partition_id),
        std::nullopt,
    };
}

DestructiveScopeKind DestructiveScope::kind() const noexcept {
    return kind_;
}

const DeviceCandidateId& DestructiveScope::device_id() const noexcept {
    return device_id_;
}

const std::optional<PartitionCandidateId>& DestructiveScope::partition_id() const noexcept {
    return partition_id_;
}

const std::optional<UnallocatedRegionId>& DestructiveScope::free_region_id() const noexcept {
    return free_region_id_;
}

void DevicePreparationPlan::validate() const {
    require_identifier(id, "device preparation plan identifier is empty");
    require_identifier(topology_generation, "device preparation topology generation is empty");
    require_identifier(device_id, "device preparation device identifier is empty");
    if (destructive_scope.device_id() != device_id)
        throw ValidationError("device preparation scope selects a different device");

    const bool whole_device = mode == ProvisioningMode::EraseWholeDevice;
    const bool existing_partition = mode == ProvisioningMode::ReformatExistingPartition;
    const bool free_space = mode == ProvisioningMode::CreatePartitionInUnallocatedSpace;
    const bool adopt = mode == ProvisioningMode::AdoptExistingTarget;
    if (!whole_device && !existing_partition && !free_space && !adopt)
        throw ValidationError("device preparation plan mode is invalid");
    if (whole_device &&
        (partition_id.has_value() || free_region_id.has_value() || inspection_id.has_value() ||
         destructive_scope.kind() != DestructiveScopeKind::WholeDevice))
        throw ValidationError("whole-device preparation plan has a contradictory target");
    if (existing_partition &&
        (!partition_id.has_value() || free_region_id.has_value() || inspection_id.has_value() ||
         destructive_scope.kind() != DestructiveScopeKind::ExistingPartition ||
         destructive_scope.partition_id() != partition_id))
        throw ValidationError("partition preparation plan has a contradictory target");
    if (free_space &&
        (partition_id.has_value() || !free_region_id.has_value() || inspection_id.has_value() ||
         destructive_scope.kind() != DestructiveScopeKind::UnallocatedRegion ||
         destructive_scope.free_region_id() != free_region_id))
        throw ValidationError("free-space preparation plan has a contradictory target");
    if (adopt &&
        (!partition_id.has_value() || free_region_id.has_value() || !inspection_id.has_value() ||
         inspection_id->empty() || destructive_scope.kind() != DestructiveScopeKind::None ||
         destructive_scope.partition_id() != partition_id))
        throw ValidationError("adoption preparation plan has a contradictory target");
}

std::string provisioning_mode_name(ProvisioningMode mode) {
    switch (mode) {
    case ProvisioningMode::EraseWholeDevice:
        return "erase-whole-device";
    case ProvisioningMode::ReformatExistingPartition:
        return "reformat-existing-partition";
    case ProvisioningMode::CreatePartitionInUnallocatedSpace:
        return "create-partition-in-unallocated-space";
    case ProvisioningMode::AdoptExistingTarget:
        return "adopt-existing-target";
    }
    return "unsupported";
}

std::optional<ProvisioningMode> provisioning_mode_from_name(std::string_view name) {
    if (name == "erase-whole-device")
        return ProvisioningMode::EraseWholeDevice;
    if (name == "reformat-existing-partition")
        return ProvisioningMode::ReformatExistingPartition;
    if (name == "create-partition-in-unallocated-space")
        return ProvisioningMode::CreatePartitionInUnallocatedSpace;
    if (name == "adopt-existing-target")
        return ProvisioningMode::AdoptExistingTarget;
    return std::nullopt;
}

std::string predicted_region_kind_name(PredictedRegionKind kind) {
    switch (kind) {
    case PredictedRegionKind::ExistingPartition:
        return "existing-partition";
    case PredictedRegionKind::Unallocated:
        return "unallocated";
    case PredictedRegionKind::BackupPartition:
        return "backup-partition";
    }
    return "unsupported";
}

} // namespace btrfsbackup::provisioning
