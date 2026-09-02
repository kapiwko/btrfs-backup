// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/provisioning/StorageSafetyInspector.hpp>

#include <algorithm>
#include <ranges>
#include <string>

namespace btrfsbackup::daemon::provisioning {
namespace {

void add_blocker(std::vector<SafetyBlocker>& result, std::string code, std::string detail = {}) {
    if (std::ranges::find(result, SafetyBlocker{code, detail}) == result.end())
        result.push_back({std::move(code), std::move(detail)});
}

void add_blockers(std::vector<SafetyBlocker>& result, const std::vector<SafetyBlocker>& blockers) {
    for (const auto& blocker : blockers)
        add_blocker(result, blocker.code, blocker.detail);
}

bool same_identity(const StableBlockDeviceIdentity& expected, const StableBlockDeviceIdentity& current) {
    return expected.display_path == current.display_path && expected.major_minor == current.major_minor &&
        expected.sysfs_path == current.sysfs_path && expected.wwn == current.wwn &&
        expected.serial == current.serial && expected.serial_short == current.serial_short &&
        expected.size_bytes == current.size_bytes;
}

const StorageDevice* expected_device(const StorageTopology& topology, const DeviceCandidateId& id) {
    const auto device = std::ranges::find(topology.devices, id, &StorageDevice::candidate_id);
    return device == topology.devices.end() ? nullptr : &*device;
}

const StorageDevice* current_device(const StorageTopology& topology, const StorageDevice& expected) {
    const auto device = std::ranges::find_if(topology.devices, [&](const auto& candidate) {
        return candidate.identity.major_minor == expected.identity.major_minor;
    });
    return device == topology.devices.end() ? nullptr : &*device;
}

const ExistingPartition* expected_partition(const StorageDevice& device, const PartitionCandidateId& id) {
    for (const auto& region : device.regions) {
        const auto* partition = std::get_if<ExistingPartition>(&region);
        if (partition != nullptr && partition->candidate_id == id)
            return partition;
    }
    return nullptr;
}

const ExistingPartition* current_partition(const StorageDevice& device, const ExistingPartition& expected) {
    for (const auto& region : device.regions) {
        const auto* partition = std::get_if<ExistingPartition>(&region);
        if (partition != nullptr && partition->partition_number == expected.partition_number)
            return partition;
    }
    return nullptr;
}

const UnallocatedRegion* expected_free_region(const StorageDevice& device, const UnallocatedRegionId& id) {
    for (const auto& region : device.regions) {
        const auto* free_region = std::get_if<UnallocatedRegion>(&region);
        if (free_region != nullptr && free_region->id == id)
            return free_region;
    }
    return nullptr;
}

const UnallocatedRegion* current_free_region(const StorageDevice& device, const UnallocatedRegion& expected) {
    for (const auto& region : device.regions) {
        const auto* free_region = std::get_if<UnallocatedRegion>(&region);
        if (free_region != nullptr && free_region->start_sector == expected.start_sector &&
            free_region->sector_count == expected.sector_count)
            return free_region;
    }
    return nullptr;
}

void inspect_device_identity(
    std::vector<SafetyBlocker>& result,
    const StorageDevice& expected,
    const StorageDevice& current,
    bool require_writable
) {
    if (!same_identity(expected.identity, current.identity))
        add_blocker(result, "block-device-identity-changed", expected.identity.display_path);
    if (expected.logical_sector_size != current.logical_sector_size ||
        expected.physical_sector_size != current.physical_sector_size)
        add_blocker(result, "sector-size-changed", expected.identity.display_path);
    if (expected.partition_table != current.partition_table)
        add_blocker(result, "partition-table-changed", expected.identity.display_path);
    if (require_writable && current.read_only)
        add_blocker(result, "read-only-device", current.identity.display_path);
    add_blockers(result, current.blockers);
}

void inspect_partition_state(
    std::vector<SafetyBlocker>& result,
    const ExistingPartition& expected,
    const ExistingPartition& current
) {
    if (!same_identity(expected.identity, current.identity) ||
        expected.partition_uuid != current.partition_uuid ||
        expected.partition_number != current.partition_number || expected.start_sector != current.start_sector ||
        expected.sector_count != current.sector_count)
        add_blocker(result, "partition-identity-changed", expected.identity.display_path);
    if (expected.filesystem != current.filesystem)
        add_blocker(result, "partition-signature-changed", expected.identity.display_path);
    add_blockers(result, current.blockers);
    for (const auto& mount_point : current.mount_points)
        add_blocker(result, "mounted-filesystem", mount_point);
    if (current.active_swap)
        add_blocker(result, "active-swap", current.identity.display_path);
    for (const auto& holder : current.holders)
        add_blocker(result, "block-holder", holder);
    if (current.configured_backup_target)
        add_blocker(result, "configured-backup-target", current.identity.display_path);
}

} // namespace

std::vector<SafetyBlocker> StorageSafetyInspector::inspect(
    const StorageTopology& expected,
    const StorageTopology& current,
    const DevicePreparationPlan& plan
) const {
    std::vector<SafetyBlocker> result;
    const StorageDevice* expected_parent = expected_device(expected, plan.device_id);
    if (expected_parent == nullptr) {
        add_blocker(result, "planned-device-missing");
        return result;
    }
    const StorageDevice* current_parent = current_device(current, *expected_parent);
    if (current_parent == nullptr) {
        add_blocker(result, "block-device-missing", expected_parent->identity.display_path);
        return result;
    }
    inspect_device_identity(
        result,
        *expected_parent,
        *current_parent,
        plan.mode != ProvisioningMode::AdoptExistingTarget
    );

    if (plan.destructive_scope.kind == DestructiveScopeKind::WholeDevice) {
        if (expected.generation != current.generation)
            add_blocker(result, "topology-generation-changed");
        for (const auto& region : current_parent->regions) {
            const auto* partition = std::get_if<ExistingPartition>(&region);
            if (partition != nullptr)
                inspect_partition_state(result, *partition, *partition);
        }
        for (const auto& region : expected_parent->regions) {
            const auto* expected_child = std::get_if<ExistingPartition>(&region);
            if (expected_child == nullptr)
                continue;
            const ExistingPartition* current_child = current_partition(*current_parent, *expected_child);
            if (current_child == nullptr)
                add_blocker(result, "block-partition-missing", expected_child->identity.display_path);
            else
                inspect_partition_state(result, *expected_child, *current_child);
        }
        return result;
    }

    if (plan.destructive_scope.kind == DestructiveScopeKind::UnallocatedRegion) {
        if (expected.generation != current.generation)
            add_blocker(result, "topology-generation-changed");
        if (!plan.free_region_id.has_value()) {
            add_blocker(result, "planned-free-region-missing");
            return result;
        }
        const UnallocatedRegion* expected_region = expected_free_region(*expected_parent, *plan.free_region_id);
        if (expected_region == nullptr) {
            add_blocker(result, "planned-free-region-missing");
            return result;
        }
        const UnallocatedRegion* current_region = current_free_region(*current_parent, *expected_region);
        if (current_region == nullptr)
            add_blocker(result, "unallocated-region-changed");
        else {
            add_blockers(result, current_region->blockers);
            if (!current_region->suitable_for_backup_partition)
                add_blocker(result, "unallocated-region-unsuitable");
        }
        for (const auto& region : current_parent->regions) {
            const auto* partition = std::get_if<ExistingPartition>(&region);
            if (partition != nullptr)
                inspect_partition_state(result, *partition, *partition);
        }
        for (const auto& region : expected_parent->regions) {
            const auto* expected_child = std::get_if<ExistingPartition>(&region);
            if (expected_child == nullptr)
                continue;
            const ExistingPartition* current_child = current_partition(*current_parent, *expected_child);
            if (current_child == nullptr)
                add_blocker(result, "block-partition-missing", expected_child->identity.display_path);
            else
                inspect_partition_state(result, *expected_child, *current_child);
        }
        return result;
    }

    const bool existing_partition_scope =
        plan.destructive_scope.kind == DestructiveScopeKind::ExistingPartition ||
        (plan.mode == ProvisioningMode::AdoptExistingTarget &&
         plan.destructive_scope.kind == DestructiveScopeKind::None);
    if (!existing_partition_scope || !plan.partition_id.has_value()) {
        add_blocker(result, "unsupported-destructive-scope");
        return result;
    }
    const ExistingPartition* expected_target = expected_partition(*expected_parent, *plan.partition_id);
    if (expected_target == nullptr) {
        add_blocker(result, "planned-partition-missing");
        return result;
    }
    const ExistingPartition* current_target = current_partition(*current_parent, *expected_target);
    if (current_target == nullptr) {
        add_blocker(result, "block-partition-missing", expected_target->identity.display_path);
        return result;
    }
    inspect_partition_state(result, *expected_target, *current_target);
    return result;
}

} // namespace btrfsbackup::daemon::provisioning
