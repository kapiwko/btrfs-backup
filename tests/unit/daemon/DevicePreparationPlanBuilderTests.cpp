// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/provisioning/DevicePreparationPlanBuilder.hpp>

#include <utility>
#include <variant>

#include <core/Errors.hpp>

#include "support/TestHelpers.hpp"

namespace {

using btrfsbackup::daemon::provisioning::CreateBackupPartition;
using btrfsbackup::daemon::provisioning::DestructiveScopeKind;
using btrfsbackup::daemon::provisioning::DevicePreparationPlanBuilder;
using btrfsbackup::daemon::provisioning::EraseDeviceSignatures;
using btrfsbackup::daemon::provisioning::ErasePartitionSignatures;
using btrfsbackup::daemon::provisioning::ExistingPartition;
using btrfsbackup::daemon::provisioning::PartitionTableType;
using btrfsbackup::daemon::provisioning::PlannedPartitionGeometry;
using btrfsbackup::daemon::provisioning::PredictedRegionKind;
using btrfsbackup::daemon::provisioning::ProvisioningMode;
using btrfsbackup::daemon::provisioning::PublishProfile;
using btrfsbackup::daemon::provisioning::StorageDevice;
using btrfsbackup::daemon::provisioning::StorageRegion;
using btrfsbackup::daemon::provisioning::StorageTopology;
using btrfsbackup::daemon::provisioning::UnallocatedRegion;

StorageTopology topology() {
    ExistingPartition partition{
        .candidate_id = "partition-1",
        .identity = {.display_path = "/dev/test1", .major_minor = "8:17", .size_bytes = 4096},
        .partition_uuid = "part-uuid",
        .partition_number = 1,
        .start_sector = 2048,
        .sector_count = 8,
        .filesystem = {.type = "ext4"},
    };
    StorageDevice device{
        .candidate_id = "device-8:16",
        .identity = {
            .display_path = "/dev/test",
            .major_minor = "8:16",
            .sysfs_path = "/devices/test",
            .serial = "serial-test",
            .size_bytes = 8192,
        },
        .display_name = "Test disk",
        .transport = "usb",
        .size_bytes = 8192,
        .logical_sector_size = 512,
        .physical_sector_size = 4096,
        .partition_table = {.type = PartitionTableType::Gpt, .identifier = "pt-uuid"},
        .regions = {
            StorageRegion{partition},
            StorageRegion{UnallocatedRegion{
                .id = "free-1",
                .start_sector = 10,
                .sector_count = 6,
                .suitable_for_backup_partition = true,
            }},
        },
    };
    return {.generation = "topology-test", .devices = {std::move(device)}};
}

void test_builds_before_and_after_preview_for_whole_device() {
    const auto plan = DevicePreparationPlanBuilder{}.build(
        topology(),
        "topology-test",
        "device-8:16",
        ProvisioningMode::EraseWholeDevice,
        "plan-test"
    );
    test_helpers::expect_eq("plan id", plan.id, "plan-test");
    test_helpers::expect_true(
        "whole-device scope",
        plan.destructive_scope.kind == DestructiveScopeKind::WholeDevice,
        "destructive scope changed"
    );
    test_helpers::expect_true("before regions", plan.before.regions.size() == 2, "current layout was lost");
    test_helpers::expect_true(
        "partition erased",
        plan.before.regions.front().data_will_be_erased,
        "existing partition is not marked destructive"
    );
    test_helpers::expect_true(
        "free space not erased",
        !plan.before.regions.back().data_will_be_erased,
        "unallocated space was described as data"
    );
    test_helpers::expect_true(
        "after GPT",
        plan.after.partition_table_type == PartitionTableType::Gpt && plan.after.regions.size() == 1,
        "predicted GPT layout is incomplete"
    );
    const auto& target = plan.after.regions.front();
    test_helpers::expect_true(
        "encrypted Btrfs target",
        target.kind == PredictedRegionKind::BackupPartition && target.encrypted &&
            target.filesystem_type == "btrfs" && !target.geometry_exact,
        "predicted target stack is incorrect"
    );
    test_helpers::expect_true("operation count", plan.operations.size() == 8, "operation sequence changed");
    test_helpers::expect_true(
        "operation bounds",
        std::holds_alternative<EraseDeviceSignatures>(plan.operations.front()) &&
            std::holds_alternative<PublishProfile>(plan.operations.back()),
        "operation sequence bounds changed"
    );
}

void test_builds_partition_plan_without_changing_other_regions() {
    const auto plan = DevicePreparationPlanBuilder{}.build(
        topology(),
        "topology-test",
        "partition-1",
        ProvisioningMode::ReformatExistingPartition,
        "plan-partition"
    );
    test_helpers::expect_true(
        "partition scope",
        plan.partition_id == "partition-1" &&
            plan.destructive_scope.kind == DestructiveScopeKind::ExistingPartition &&
            plan.destructive_scope.partition_id == plan.partition_id,
        "destructive scope is not limited to the partition"
    );
    test_helpers::expect_true(
        "partition before",
        plan.before.regions.front().data_will_be_erased && !plan.before.regions.back().data_will_be_erased,
        "the wrong region is marked for erasure"
    );
    const auto& target = plan.after.regions.front();
    test_helpers::expect_true(
        "partition after",
        target.kind == PredictedRegionKind::BackupPartition && target.geometry_exact && target.changed &&
            target.encrypted && target.filesystem_type == "btrfs",
        "partition target prediction is incorrect"
    );
    test_helpers::expect_true(
        "other region unchanged",
        plan.before.regions.back() == plan.after.regions.back(),
        "an unrelated region changed"
    );
    test_helpers::expect_true(
        "partition operation bounds",
        plan.operations.size() == 6 &&
            std::holds_alternative<ErasePartitionSignatures>(plan.operations.front()) &&
            std::holds_alternative<PublishProfile>(plan.operations.back()),
        "partition operation sequence changed"
    );
}

void test_builds_free_space_plan_without_changing_existing_partition() {
    const auto plan = DevicePreparationPlanBuilder{}.build(
        topology(),
        "topology-test",
        "free-1",
        ProvisioningMode::CreatePartitionInUnallocatedSpace,
        "plan-free",
        std::nullopt,
        PlannedPartitionGeometry{.start_sector = 11, .sector_count = 4, .partition_number = 2}
    );
    test_helpers::expect_true(
        "free region scope",
        plan.free_region_id == "free-1" &&
            plan.destructive_scope.kind == DestructiveScopeKind::UnallocatedRegion &&
            plan.destructive_scope.free_region_id == plan.free_region_id,
        "plan is not limited to the selected free region"
    );
    test_helpers::expect_true(
        "existing partition preserved",
        plan.before.regions.front() == plan.after.regions.front(),
        "existing partition changed in free-space preview"
    );
    test_helpers::expect_true("free-space region count", plan.after.regions.size() == 4, "residual free space was lost");
    const auto& target = plan.after.regions.at(2);
    test_helpers::expect_true(
        "free region target",
        target.id == "planned-backup-partition" && target.kind == PredictedRegionKind::BackupPartition &&
            target.start_sector == 11 && target.sector_count == 4 && target.partition_number == 2 && target.geometry_exact &&
            target.changed && target.encrypted && target.filesystem_type == "btrfs",
        "free-space target prediction is incorrect"
    );
    test_helpers::expect_true(
        "residual free regions",
        plan.after.regions.at(1).start_sector == 10 && plan.after.regions.at(1).sector_count == 1 &&
            plan.after.regions.at(3).start_sector == 15 && plan.after.regions.at(3).sector_count == 1,
        "free-space preview does not preserve alignment gaps"
    );
    test_helpers::expect_true(
        "free region operations",
        plan.operations.size() == 6 &&
            std::holds_alternative<CreateBackupPartition>(plan.operations.front()) &&
            std::get<CreateBackupPartition>(plan.operations.front()).free_region_id == plan.free_region_id &&
            std::get<CreateBackupPartition>(plan.operations.front()).geometry ==
                PlannedPartitionGeometry{.start_sector = 11, .sector_count = 4, .partition_number = 2} &&
            std::holds_alternative<PublishProfile>(plan.operations.back()),
        "free-space operation sequence changed"
    );
}

void test_rejects_free_space_plan_without_gpt() {
    auto value = topology();
    value.devices.front().partition_table.type = PartitionTableType::Mbr;
    try {
        static_cast<void>(DevicePreparationPlanBuilder{}.build(
            value,
            value.generation,
            "free-1",
            ProvisioningMode::CreatePartitionInUnallocatedSpace,
            "plan-free",
            std::nullopt,
            PlannedPartitionGeometry{.start_sector = 10, .sector_count = 6, .partition_number = 2}
        ));
        test_helpers::fail("free space on MBR", "free-space plan accepted an MBR partition table");
    } catch (const btrfsbackup::ValidationError&) {}
}

void test_rejects_invalid_free_space_geometry() {
    const auto value = topology();
    for (const auto geometry : {
             PlannedPartitionGeometry{},
             PlannedPartitionGeometry{.start_sector = 9, .sector_count = 2, .partition_number = 2},
             PlannedPartitionGeometry{.start_sector = 14, .sector_count = 3, .partition_number = 2},
         }) {
        try {
            static_cast<void>(DevicePreparationPlanBuilder{}.build(
                value,
                value.generation,
                "free-1",
                ProvisioningMode::CreatePartitionInUnallocatedSpace,
                "plan-invalid-free",
                std::nullopt,
                geometry
            ));
            test_helpers::fail("invalid free-space geometry", "out-of-range partition geometry was accepted");
        } catch (const btrfsbackup::ValidationError&) {}
    }
}

void test_rejects_stale_topology_and_unimplemented_mode() {
    const auto value = topology();
    bool stale_rejected = false;
    try {
        static_cast<void>(DevicePreparationPlanBuilder{}.build(
            value,
            "older-topology",
            "device-8:16",
            ProvisioningMode::EraseWholeDevice,
            "plan-stale"
        ));
    } catch (const btrfsbackup::ValidationError&) {
        stale_rejected = true;
    }
    test_helpers::expect_true("stale topology", stale_rejected, "stale topology was accepted");

    bool mode_rejected = false;
    try {
        static_cast<void>(DevicePreparationPlanBuilder{}.build(
            value,
            value.generation,
            "device-8:16",
            ProvisioningMode::AdoptExistingTarget,
            "plan-adopt"
        ));
    } catch (const btrfsbackup::ValidationError&) {
        mode_rejected = true;
    }
    test_helpers::expect_true("unimplemented mode", mode_rejected, "unsafe mode was accepted");
}

} // namespace

int main() {
    test_builds_before_and_after_preview_for_whole_device();
    test_builds_partition_plan_without_changing_other_regions();
    test_builds_free_space_plan_without_changing_existing_partition();
    test_rejects_free_space_plan_without_gpt();
    test_rejects_invalid_free_space_geometry();
    test_rejects_stale_topology_and_unimplemented_mode();
    return test_helpers::finish("device preparation plan builder tests");
}
