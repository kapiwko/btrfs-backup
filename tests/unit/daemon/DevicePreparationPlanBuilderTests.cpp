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
    test_rejects_stale_topology_and_unimplemented_mode();
    return test_helpers::finish("device preparation plan builder tests");
}
