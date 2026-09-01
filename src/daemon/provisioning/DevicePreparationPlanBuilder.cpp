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

} // namespace

DevicePreparationPlan DevicePreparationPlanBuilder::build(
    const StorageTopology& topology,
    const TopologyGeneration& expected_generation,
    const DeviceCandidateId& device_id,
    ProvisioningMode mode,
    DevicePreparationPlanId plan_id
) const {
    if (topology.generation.empty() || topology.generation != expected_generation)
        throw ValidationError("storage topology generation changed");
    if (plan_id.empty())
        throw ValidationError("device preparation plan identifier is empty");
    const auto device = std::ranges::find(topology.devices, device_id, &StorageDevice::candidate_id);
    if (device == topology.devices.end())
        throw ValidationError("storage device candidate is unavailable");
    if (mode != ProvisioningMode::EraseWholeDevice)
        throw ValidationError("provisioning mode is not implemented");
    return erase_whole_device_plan(topology, *device, std::move(plan_id));
}

} // namespace btrfsbackup::daemon::provisioning
