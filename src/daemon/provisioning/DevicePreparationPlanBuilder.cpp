// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/provisioning/DevicePreparationPlanBuilder.hpp>

#include <algorithm>
#include <iterator>
#include <ranges>
#include <type_traits>
#include <utility>

#include <core/Errors.hpp>

namespace btrfsbackup::daemon::provisioning {
namespace {

PredictedStorageRegion predicted_region(const StorageRegion& region) {
    return std::visit(
        [](const auto& value) {
            PredictedStorageRegion result;
            result.start_sector = value.start_sector;
            result.sector_count = value.sector_count;
            if constexpr (std::is_same_v<std::decay_t<decltype(value)>, ExistingPartition>) {
                result.id = value.candidate_id;
                result.kind = PredictedRegionKind::ExistingPartition;
                result.partition_number = value.partition_number;
                result.display_path = value.identity.display_path;
                result.partition_label = value.partition_label.value_or("");
                result.filesystem_type = value.filesystem.type;
            } else {
                result.id = value.id;
                result.kind = PredictedRegionKind::Unallocated;
            }
            return result;
        },
        region
    );
}

StorageLayout current_layout(const StorageDevice& device) {
    StorageLayout result;
    result.device_id = device.candidate_id;
    result.size_bytes = device.size_bytes;
    result.logical_sector_size = device.logical_sector_size;
    result.partition_table_type = device.partition_table.type;
    result.regions.reserve(device.regions.size());
    std::ranges::transform(device.regions, std::back_inserter(result.regions), predicted_region);
    return result;
}

DevicePreparationPlan erase_whole_device_plan(
    const StorageTopology& topology,
    const StorageDevice& device,
    DevicePreparationPlanId plan_id
) {
    StorageLayout before = current_layout(device);
    for (auto& region : before.regions)
        region.data_will_be_erased = region.kind == PredictedRegionKind::ExistingPartition;
    const std::uint64_t sectors = device.logical_sector_size == 0
        ? 0
        : device.size_bytes / device.logical_sector_size;
    StorageLayout after;
    after.device_id = device.candidate_id;
    after.size_bytes = device.size_bytes;
    after.logical_sector_size = device.logical_sector_size;
    after.partition_table_type = PartitionTableType::Gpt;
    PredictedStorageRegion target;
    target.id = "planned-backup-partition";
    target.kind = PredictedRegionKind::BackupPartition;
    target.sector_count = sectors;
    target.partition_number = 1;
    target.partition_label = "btrfs-backup";
    target.filesystem_type = "btrfs";
    target.geometry_exact = false;
    target.encrypted = true;
    target.changed = true;
    after.regions.push_back(std::move(target));
    std::vector<SafetyWarning> warnings{{"erase-whole-device", device.identity.display_path}};
    for (const auto& blocker : device.blockers)
        warnings.push_back({blocker.code, blocker.detail});
    DevicePreparationPlan result;
    result.id = std::move(plan_id);
    result.topology_generation = topology.generation;
    result.mode = ProvisioningMode::EraseWholeDevice;
    result.device_id = device.candidate_id;
    result.before = std::move(before);
    result.after = std::move(after);
    result.operations = {
        EraseDeviceSignatures{device.candidate_id},
        CreateGptPartitionTable{device.candidate_id},
        CreateBackupPartition{device.candidate_id, std::nullopt},
        FormatLuks2{std::nullopt},
        OpenLuksMapping{},
        FormatBtrfs{},
        VerifyPreparedTarget{},
        PublishProfile{},
    };
    result.warnings = std::move(warnings);
    result.destructive_scope.kind = DestructiveScopeKind::WholeDevice;
    result.destructive_scope.device_id = device.candidate_id;
    return result;
}

DevicePreparationPlan reformat_partition_plan(
    const StorageTopology& topology,
    const StorageDevice& device,
    const ExistingPartition& partition,
    DevicePreparationPlanId plan_id
) {
    StorageLayout before = current_layout(device);
    const auto before_target = std::ranges::find(before.regions, partition.candidate_id, &PredictedStorageRegion::id);
    before_target->data_will_be_erased = true;
    StorageLayout after = current_layout(device);
    const auto after_target = std::ranges::find(after.regions, partition.candidate_id, &PredictedStorageRegion::id);
    after_target->kind = PredictedRegionKind::BackupPartition;
    after_target->filesystem_type = "btrfs";
    after_target->partition_label = "btrfs-backup";
    after_target->encrypted = true;
    after_target->changed = true;
    std::vector<SafetyWarning> warnings{{"erase-existing-partition", partition.identity.display_path}};
    for (const auto& blocker : partition.blockers)
        warnings.push_back({blocker.code, blocker.detail});
    DevicePreparationPlan result;
    result.id = std::move(plan_id);
    result.topology_generation = topology.generation;
    result.mode = ProvisioningMode::ReformatExistingPartition;
    result.device_id = device.candidate_id;
    result.partition_id = partition.candidate_id;
    result.before = std::move(before);
    result.after = std::move(after);
    result.operations = {
        ErasePartitionSignatures{partition.candidate_id},
        FormatLuks2{partition.candidate_id},
        OpenLuksMapping{},
        FormatBtrfs{},
        VerifyPreparedTarget{},
        PublishProfile{},
    };
    result.warnings = std::move(warnings);
    result.destructive_scope.kind = DestructiveScopeKind::ExistingPartition;
    result.destructive_scope.device_id = device.candidate_id;
    result.destructive_scope.partition_id = partition.candidate_id;
    return result;
}

DevicePreparationPlan create_partition_in_free_space_plan(
    const StorageTopology& topology,
    const StorageDevice& device,
    const UnallocatedRegion& free_region,
    DevicePreparationPlanId plan_id
) {
    if (device.partition_table.type != PartitionTableType::Gpt)
        throw ValidationError("creating a backup partition requires GPT");
    if (!free_region.suitable_for_backup_partition || !free_region.blockers.empty() || free_region.sector_count == 0)
        throw ValidationError("unallocated region is not suitable for a backup partition");
    StorageLayout before = current_layout(device);
    StorageLayout after = before;
    const auto target = std::ranges::find(after.regions, free_region.id, &PredictedStorageRegion::id);
    target->id = "planned-backup-partition";
    target->kind = PredictedRegionKind::BackupPartition;
    target->partition_label = "btrfs-backup";
    target->filesystem_type = "btrfs";
    target->geometry_exact = false;
    target->encrypted = true;
    target->changed = true;

    DevicePreparationPlan result;
    result.id = std::move(plan_id);
    result.topology_generation = topology.generation;
    result.mode = ProvisioningMode::CreatePartitionInUnallocatedSpace;
    result.device_id = device.candidate_id;
    result.free_region_id = free_region.id;
    result.before = std::move(before);
    result.after = std::move(after);
    result.operations = {
        CreateBackupPartition{device.candidate_id, free_region.id},
        FormatLuks2{std::nullopt},
        OpenLuksMapping{},
        FormatBtrfs{},
        VerifyPreparedTarget{},
        PublishProfile{},
    };
    result.warnings = {{"create-partition-in-unallocated-space", device.identity.display_path}};
    result.destructive_scope.kind = DestructiveScopeKind::UnallocatedRegion;
    result.destructive_scope.device_id = device.candidate_id;
    result.destructive_scope.free_region_id = free_region.id;
    return result;
}

DevicePreparationPlan adopt_existing_target_plan(
    const StorageTopology& topology,
    const StorageDevice& device,
    const ExistingPartition& partition,
    DevicePreparationPlanId plan_id,
    std::string inspection_id
) {
    if (!partition.suitable_for_adoption || inspection_id.empty())
        throw ValidationError("storage partition requires a valid existing target inspection");
    DevicePreparationPlan result;
    result.id = std::move(plan_id);
    result.topology_generation = topology.generation;
    result.mode = ProvisioningMode::AdoptExistingTarget;
    result.device_id = device.candidate_id;
    result.partition_id = partition.candidate_id;
    result.inspection_id = std::move(inspection_id);
    result.before = current_layout(device);
    result.after = result.before;
    result.operations = {VerifyPreparedTarget{}, PublishProfile{}};
    result.destructive_scope.kind = DestructiveScopeKind::None;
    result.destructive_scope.device_id = device.candidate_id;
    result.destructive_scope.partition_id = partition.candidate_id;
    return result;
}

} // namespace

DevicePreparationPlan DevicePreparationPlanBuilder::build(
    const StorageTopology& topology,
    const TopologyGeneration& expected_generation,
    const std::string& selected_candidate_id,
    ProvisioningMode mode,
    DevicePreparationPlanId plan_id,
    std::optional<std::string> inspection_id
) const {
    if (topology.generation.empty() || topology.generation != expected_generation)
        throw ValidationError("storage topology generation changed");
    if (plan_id.empty())
        throw ValidationError("device preparation plan identifier is empty");
    if (mode == ProvisioningMode::AdoptExistingTarget && !inspection_id.has_value())
        throw ValidationError("existing target inspection identifier is required");
    if (mode == ProvisioningMode::EraseWholeDevice) {
        const auto device = std::ranges::find(topology.devices, selected_candidate_id, &StorageDevice::candidate_id);
        if (device == topology.devices.end())
            throw ValidationError("storage device candidate is unavailable");
        return erase_whole_device_plan(topology, *device, std::move(plan_id));
    }
    if (mode == ProvisioningMode::ReformatExistingPartition || mode == ProvisioningMode::AdoptExistingTarget) {
        for (const auto& device : topology.devices) {
            const auto region = std::ranges::find_if(device.regions, [&](const StorageRegion& value) {
                const auto* partition = std::get_if<ExistingPartition>(&value);
                return partition != nullptr && partition->candidate_id == selected_candidate_id;
            });
            if (region != device.regions.end() && mode == ProvisioningMode::ReformatExistingPartition)
                return reformat_partition_plan(
                    topology,
                    device,
                    std::get<ExistingPartition>(*region),
                    std::move(plan_id)
                );
            if (region != device.regions.end() && inspection_id.has_value())
                return adopt_existing_target_plan(
                    topology,
                    device,
                    std::get<ExistingPartition>(*region),
                    std::move(plan_id),
                    std::move(*inspection_id)
                );
        }
        throw ValidationError("storage partition candidate is unavailable");
    }
    if (mode == ProvisioningMode::CreatePartitionInUnallocatedSpace) {
        for (const auto& device : topology.devices) {
            const auto region = std::ranges::find_if(device.regions, [&](const StorageRegion& value) {
                const auto* free_region = std::get_if<UnallocatedRegion>(&value);
                return free_region != nullptr && free_region->id == selected_candidate_id;
            });
            if (region != device.regions.end())
                return create_partition_in_free_space_plan(
                    topology,
                    device,
                    std::get<UnallocatedRegion>(*region),
                    std::move(plan_id)
                );
        }
        throw ValidationError("unallocated storage region candidate is unavailable");
    }
    throw ValidationError("provisioning mode is not implemented");
}

} // namespace btrfsbackup::daemon::provisioning
