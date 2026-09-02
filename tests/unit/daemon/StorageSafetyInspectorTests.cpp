// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/provisioning/DevicePreparationPlanBuilder.hpp>
#include <daemon/provisioning/StorageSafetyInspector.hpp>

#include <algorithm>

#include "support/TestHelpers.hpp"

namespace {

namespace provisioning = btrfsbackup::daemon::provisioning;

provisioning::ExistingPartition partition(
    std::string id,
    std::string path,
    std::string number,
    std::uint32_t partition_number,
    std::uint64_t start
) {
    return {
        .candidate_id = std::move(id),
        .identity = {
            .display_path = std::move(path),
            .major_minor = std::move(number),
            .sysfs_path = "/devices/test/partition-" + std::to_string(partition_number),
            .size_bytes = 4096,
        },
        .partition_uuid = "part-" + std::to_string(partition_number),
        .partition_number = partition_number,
        .start_sector = start,
        .sector_count = 8,
        .filesystem = {.type = "ext4", .uuid = "fs-" + std::to_string(partition_number)},
        .suitable_for_reformat = true,
    };
}

provisioning::StorageTopology topology(std::string generation = "generation-1") {
    provisioning::StorageDevice device{
        .candidate_id = "opaque-device",
        .identity = {
            .display_path = "/dev/test",
            .major_minor = "8:16",
            .sysfs_path = "/devices/test",
            .wwn = "wwn-test",
            .serial = "serial-test",
            .serial_short = "short-test",
            .size_bytes = 16384,
        },
        .display_name = "Test disk",
        .transport = "usb",
        .size_bytes = 16384,
        .logical_sector_size = 512,
        .physical_sector_size = 4096,
        .partition_table = {.type = provisioning::PartitionTableType::Gpt, .identifier = "pt-uuid"},
        .regions = {
            provisioning::StorageRegion{partition("opaque-1", "/dev/test1", "8:17", 1, 8)},
            provisioning::StorageRegion{partition("opaque-2", "/dev/test2", "8:18", 2, 24)},
        },
    };
    return {.generation = std::move(generation), .devices = {std::move(device)}};
}

provisioning::StorageTopology topology_with_free_space(std::string generation = "generation-1") {
    auto result = topology(std::move(generation));
    result.devices.front().regions.emplace_back(provisioning::UnallocatedRegion{
        .id = "free-1",
        .start_sector = 40,
        .sector_count = 16,
        .suitable_for_backup_partition = true,
    });
    return result;
}

bool contains_code(const std::vector<provisioning::SafetyBlocker>& blockers, const std::string& code) {
    return std::ranges::any_of(blockers, [&](const auto& blocker) { return blocker.code == code; });
}

void test_partition_scope_ignores_mounted_sibling() {
    const auto expected = topology();
    auto current = topology("generation-with-mounted-sibling");
    auto& sibling = std::get<provisioning::ExistingPartition>(current.devices.front().regions.back());
    sibling.mount_points = {"/media/data"};
    sibling.blockers = {{"mounted-filesystem", "/media/data"}};
    auto plan = provisioning::DevicePreparationPlanBuilder{}.build(
        expected,
        expected.generation,
        "opaque-1",
        provisioning::ProvisioningMode::ReformatExistingPartition,
        "plan-partition"
    );
    const auto blockers = provisioning::StorageSafetyInspector{}.inspect(expected, current, plan);
    test_helpers::expect_true(
        "mounted sibling",
        blockers.empty(),
        "an unrelated mounted partition blocked the selected partition"
    );
}

void test_partition_scope_detects_target_changes_and_usage() {
    const auto expected = topology();
    auto current = topology("generation-changed");
    auto& target = std::get<provisioning::ExistingPartition>(current.devices.front().regions.front());
    target.filesystem.uuid = "replacement-filesystem";
    target.mount_points = {"/media/target"};
    target.holders = {"dm-0"};
    target.active_swap = true;
    auto plan = provisioning::DevicePreparationPlanBuilder{}.build(
        expected,
        expected.generation,
        "opaque-1",
        provisioning::ProvisioningMode::ReformatExistingPartition,
        "plan-partition"
    );
    const auto blockers = provisioning::StorageSafetyInspector{}.inspect(expected, current, plan);
    test_helpers::expect_true(
        "partition signature",
        contains_code(blockers, "partition-signature-changed"),
        "a replaced filesystem was accepted"
    );
    test_helpers::expect_true("partition mount", contains_code(blockers, "mounted-filesystem"), "mount was ignored");
    test_helpers::expect_true("partition holder", contains_code(blockers, "block-holder"), "holder was ignored");
    test_helpers::expect_true("partition swap", contains_code(blockers, "active-swap"), "swap was ignored");
}

void test_partition_scope_detects_parent_and_geometry_changes() {
    const auto expected = topology();
    auto current = topology("generation-changed");
    current.devices.front().partition_table.identifier = "other-table";
    auto& target = std::get<provisioning::ExistingPartition>(current.devices.front().regions.front());
    target.start_sector += 1;
    auto plan = provisioning::DevicePreparationPlanBuilder{}.build(
        expected,
        expected.generation,
        "opaque-1",
        provisioning::ProvisioningMode::ReformatExistingPartition,
        "plan-partition"
    );
    const auto blockers = provisioning::StorageSafetyInspector{}.inspect(expected, current, plan);
    test_helpers::expect_true(
        "partition table",
        contains_code(blockers, "partition-table-changed"),
        "a replaced partition table was accepted"
    );
    test_helpers::expect_true(
        "partition geometry",
        contains_code(blockers, "partition-identity-changed"),
        "changed partition geometry was accepted"
    );
}

void test_whole_device_scope_rejects_any_topology_or_child_usage_change() {
    const auto expected = topology();
    auto current = topology();
    auto& sibling = std::get<provisioning::ExistingPartition>(current.devices.front().regions.back());
    sibling.mount_points = {"/media/data"};
    auto& target = std::get<provisioning::ExistingPartition>(current.devices.front().regions.front());
    target.start_sector += 1;
    auto plan = provisioning::DevicePreparationPlanBuilder{}.build(
        expected,
        expected.generation,
        "opaque-device",
        provisioning::ProvisioningMode::EraseWholeDevice,
        "plan-device"
    );
    const auto blockers = provisioning::StorageSafetyInspector{}.inspect(expected, current, plan);
    test_helpers::expect_true(
        "whole identity",
        contains_code(blockers, "partition-identity-changed"),
        "a child identity change hidden by the generation was accepted"
    );
    test_helpers::expect_true(
        "whole child mount",
        contains_code(blockers, "mounted-filesystem"),
        "mounted child was ignored for whole-device erasure"
    );
}

void test_free_space_scope_requires_exact_range_and_inactive_disk() {
    const auto expected = topology_with_free_space();
    auto plan = provisioning::DevicePreparationPlanBuilder{}.build(
        expected,
        expected.generation,
        "free-1",
        provisioning::ProvisioningMode::CreatePartitionInUnallocatedSpace,
        "plan-free",
        std::nullopt,
        provisioning::PlannedPartitionGeometry{
            .start_sector = 40,
            .sector_count = 16,
            .partition_number = 2,
        }
    );
    test_helpers::expect_true(
        "unchanged free range",
        provisioning::StorageSafetyInspector{}.inspect(expected, expected, plan).empty(),
        "unchanged free-space plan was rejected"
    );
    auto current = topology_with_free_space("generation-changed");
    auto& free_region = std::get<provisioning::UnallocatedRegion>(current.devices.front().regions.back());
    free_region.start_sector += 1;
    auto& sibling = std::get<provisioning::ExistingPartition>(current.devices.front().regions.front());
    sibling.mount_points = {"/media/data"};
    sibling.start_sector += 1;
    const auto blockers = provisioning::StorageSafetyInspector{}.inspect(expected, current, plan);
    test_helpers::expect_true(
        "free range changed",
        contains_code(blockers, "unallocated-region-changed") &&
            contains_code(blockers, "topology-generation-changed"),
        "changed free-space geometry was accepted"
    );
    test_helpers::expect_true(
        "free-space sibling mount",
        contains_code(blockers, "mounted-filesystem") && contains_code(blockers, "partition-identity-changed"),
        "changed or mounted partition did not block partition-table mutation"
    );
}

} // namespace

int main() {
    test_partition_scope_ignores_mounted_sibling();
    test_partition_scope_detects_target_changes_and_usage();
    test_partition_scope_detects_parent_and_geometry_changes();
    test_whole_device_scope_rejects_any_topology_or_child_usage_change();
    test_free_space_scope_requires_exact_range_and_inactive_disk();
    return test_helpers::finish("storage safety inspector tests");
}
