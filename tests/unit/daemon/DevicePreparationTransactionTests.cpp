// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/DevicePreparationTransaction.hpp>

#include <core/Errors.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <ranges>
#include <utility>

#include "support/TestHelpers.hpp"

namespace {

using btrfsbackup::daemon::control::DevicePreparationTransaction;
using btrfsbackup::daemon::control::DevicePreparationTransactionStore;

std::int64_t now_seconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch()
    )
        .count();
}

DevicePreparationTransaction transaction(
    const std::string& operation_id,
    const std::string& state,
    std::int64_t updated_at
) {
    DevicePreparationTransaction value;
    value.status.operation_id = operation_id;
    value.status.profile_id = "test";
    value.status.state = state;
    value.status.phase = "mkfs-btrfs";
    value.status.recovery_action = "inspect manually";
    value.owner = {.bus_name = ":1.42", .uid = 1000};
    value.device.path = "/dev/test";
    value.device.major_minor = "8:16";
    value.device.sysfs_devpath = "/devices/test/block/test";
    value.device.wwn = "wwn-test";
    value.target.mode = btrfsbackup::daemon::provisioning::ProvisioningMode::ReformatExistingPartition;
    value.target.device.identity = {
        .display_path = "/dev/test",
        .major_minor = "8:16",
        .sysfs_path = "/devices/test/block/test",
        .wwn = "wwn-test",
        .size_bytes = 1048576,
    };
    value.target.device.size_bytes = 1048576;
    value.target.device.transport = "usb";
    value.target.device.logical_sector_size = 512;
    value.target.device.physical_sector_size = 4096;
    value.target.device.partition_table = {
        .type = btrfsbackup::daemon::provisioning::PartitionTableType::Gpt,
        .identifier = "pt-uuid",
    };
    value.target.partition = btrfsbackup::daemon::provisioning::ExistingPartition{
        .identity = {
            .display_path = "/dev/test1",
            .major_minor = "8:17",
            .sysfs_path = "/devices/test/block/test/test1",
            .size_bytes = 524288,
        },
        .partition_uuid = "partition-uuid",
        .partition_number = 1,
        .start_sector = 2048,
        .sector_count = 1024,
        .filesystem = {.type = "ext4", .uuid = "filesystem-uuid"},
    };
    value.profile_name = "Test";
    value.source_subvolume = "/home";
    value.passphrase_label = "Recovery";
    value.created_at = updated_at - 10;
    value.updated_at = updated_at;
    value.last_completed_phase = "luks-format";
    value.partition = "/dev/test1";
    value.luks_uuid = "luks-uuid";
    value.mapper = "btrfs-backup-test";
    value.cleanup_result = "pending";
    return value;
}

void test_round_trip_preserves_recovery_state() {
    const auto root = test_helpers::test_root("device-preparation-transactions", "round-trip");
    DevicePreparationTransactionStore store(root);
    store.save(transaction("prepare-round-trip", "running", now_seconds()));
    const auto loaded = store.load_and_prune();
    test_helpers::expect_true("round trip count", loaded.size() == 1, "transaction was not loaded");
    const auto& value = loaded.front();
    test_helpers::expect_eq("owner bus", value.owner.bus_name, ":1.42");
    test_helpers::expect_true("owner uid", value.owner.uid == 1000, "owner UID changed");
    test_helpers::expect_eq("stable identity", value.device.major_minor, "8:16");
    test_helpers::expect_true(
        "target mode",
        value.target.mode == btrfsbackup::daemon::provisioning::ProvisioningMode::ReformatExistingPartition,
        "target mode changed"
    );
    test_helpers::expect_eq("target transport", value.target.device.transport, "usb");
    test_helpers::expect_true(
        "target partition",
        value.target.partition.has_value() && value.target.partition->partition_uuid == "partition-uuid" &&
            value.target.partition->start_sector == 2048,
        "partition target snapshot changed"
    );
    test_helpers::expect_eq("last completed phase", value.last_completed_phase, "luks-format");
    test_helpers::expect_eq("partition", value.partition, "/dev/test1");
    test_helpers::expect_eq("LUKS UUID", value.luks_uuid, "luks-uuid");
    test_helpers::expect_eq("mapper", value.mapper, "btrfs-backup-test");
    test_helpers::expect_eq("recovery action", value.status.recovery_action, "inspect manually");
}

void test_round_trip_preserves_adoption_fingerprint() {
    const auto root = test_helpers::test_root("device-preparation-transactions", "adoption");
    auto value = transaction("prepare-adoption", "queued", now_seconds());
    value.target.mode = btrfsbackup::daemon::provisioning::ProvisioningMode::AdoptExistingTarget;
    value.target.expected_inspection = btrfsbackup::daemon::provisioning::ExistingTargetInspectionSummary{
        .luks_uuid = "existing-luks",
        .btrfs_uuid = "existing-btrfs",
        .partition_uuid = "partition-uuid",
        .repository_id = "repository-1",
        .catalog_generation = 8,
        .snapshot_count = 3,
    };
    DevicePreparationTransactionStore store(root);
    store.save(value);
    const auto loaded = store.load("prepare-adoption");
    test_helpers::expect_true(
        "adoption fingerprint",
        loaded.target.mode == btrfsbackup::daemon::provisioning::ProvisioningMode::AdoptExistingTarget &&
            loaded.target.expected_inspection == value.target.expected_inspection,
        "adoption inspection fingerprint changed during persistence"
    );
}

void test_round_trip_preserves_free_space_geometry() {
    const auto root = test_helpers::test_root("device-preparation-transactions", "free-space");
    auto value = transaction("prepare-free-space", "queued", now_seconds());
    value.target.mode =
        btrfsbackup::daemon::provisioning::ProvisioningMode::CreatePartitionInUnallocatedSpace;
    value.target.partition.reset();
    btrfsbackup::daemon::provisioning::UnallocatedRegion free_region;
    free_region.id = "ephemeral-candidate";
    free_region.start_sector = 4096;
    free_region.sector_count = 8192;
    free_region.suitable_for_backup_partition = true;
    value.target.free_region = std::move(free_region);
    value.target.planned_partition_geometry =
        btrfsbackup::daemon::provisioning::PlannedPartitionGeometry{
            .start_sector = 4096,
            .sector_count = 8192,
            .partition_number = 2,
        };
    DevicePreparationTransactionStore store(root);
    store.save(value);
    const auto loaded = store.load("prepare-free-space");
    test_helpers::expect_true(
        "free-space geometry",
        loaded.target.mode ==
                btrfsbackup::daemon::provisioning::ProvisioningMode::CreatePartitionInUnallocatedSpace &&
            loaded.target.free_region.has_value() && loaded.target.free_region->id.empty() &&
            loaded.target.free_region->start_sector == 4096 && loaded.target.free_region->sector_count == 8192 &&
            loaded.target.planned_partition_geometry == value.target.planned_partition_geometry,
        "free-space target geometry changed during persistence"
    );
}

void test_completed_limit_ttl_and_active_retention() {
    const auto root = test_helpers::test_root("device-preparation-transactions", "retention");
    DevicePreparationTransactionStore store(root, 2, std::chrono::hours(1));
    const std::int64_t now = now_seconds();
    store.save(transaction("prepare-expired", "failed", now - 7200));
    store.save(transaction("prepare-oldest", "succeeded", now - 30));
    store.save(transaction("prepare-middle", "failed", now - 20));
    store.save(transaction("prepare-newest", "cancelled", now - 10));
    store.save(transaction("prepare-active", "running", now - 7200));

    const auto loaded = store.load_and_prune();
    const auto contains = [&](const std::string& id) {
        return std::ranges::find(loaded, id, [](const auto& value) {
                   return value.status.operation_id;
               }) != loaded.end();
    };
    test_helpers::expect_true("retained newest", contains("prepare-newest"), "newest result was pruned");
    test_helpers::expect_true("retained middle", contains("prepare-middle"), "second newest result was pruned");
    test_helpers::expect_true("limited oldest", !contains("prepare-oldest"), "completed limit was ignored");
    test_helpers::expect_true("expired result", !contains("prepare-expired"), "completed TTL was ignored");
    test_helpers::expect_true("active retained", contains("prepare-active"), "active transaction was TTL-pruned");
}

void test_legacy_transaction_is_rejected() {
    const auto root = test_helpers::test_root("device-preparation-transactions", "legacy");
    test_helpers::write_file(
        root / "prepare-legacy.json",
        R"({
  "schemaVersion": 1,
  "operationId": "prepare-legacy",
  "profileId": "test",
  "state": "running",
  "phase": "open",
  "ownerBusName": ":1.42",
  "ownerUid": 1000,
  "device": {"path": "/dev/test", "majorMinor": "8:16"},
  "createdAt": 100,
  "updatedAt": 100,
  "mapper": "btrfs-backup-test"
})"
    );

    try {
        static_cast<void>(DevicePreparationTransactionStore(root).load("prepare-legacy"));
        test_helpers::fail("legacy transaction", "an unreleased transaction schema was accepted");
    } catch (const btrfsbackup::ValidationError&) {
    }
}

} // namespace

int main() {
    test_round_trip_preserves_recovery_state();
    test_round_trip_preserves_adoption_fingerprint();
    test_round_trip_preserves_free_space_geometry();
    test_completed_limit_ttl_and_active_retention();
    test_legacy_transaction_is_rejected();
    return test_helpers::finish("device preparation transaction tests");
}
