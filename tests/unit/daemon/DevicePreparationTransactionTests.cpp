// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/DevicePreparationTransactionCodec.hpp>
#include <daemon/control/DevicePreparationTransactionStore.hpp>

#include <core/Errors.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <ranges>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

#include "support/TestHelpers.hpp"

namespace {

using btrfsbackup::daemon::control::DevicePreparationTransaction;
using btrfsbackup::daemon::control::DevicePreparationTransactionCodec;
using btrfsbackup::daemon::control::DevicePreparationTransactionStore;
using btrfsbackup::daemon::control::TransactionRevision;

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
    value.device.mounted = true;
    value.device.contains_data = true;
    value.target.mode = btrfsbackup::provisioning::ProvisioningMode::ReformatExistingPartition;
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
        .type = btrfsbackup::provisioning::PartitionTableType::Gpt,
        .identifier = "pt-uuid",
    };
    value.target.partition = btrfsbackup::provisioning::ExistingPartition{
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
        .suitable_for_reformat = true,
        .suitable_for_adoption = true,
    };
    value.profile_name = "Test";
    value.source_subvolume = "/home";
    value.source_filesystem_uuid = "source-btrfs-uuid";
    value.source_mount_root = "/home";
    value.local_snapshot_dir = "/home/.snapshots/btrfs-backup/test";
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

void test_codec_owns_schema_and_validation() {
    DevicePreparationTransaction value = transaction("prepare-codec", "running", now_seconds());
    value.revision = TransactionRevision{7};
    const DevicePreparationTransactionCodec codec;
    const std::string document = codec.serialize(value);
    const auto decoded = codec.deserialize(document);
    test_helpers::expect_true(
        "codec round trip",
        decoded.revision == value.revision && decoded.status.operation_id == value.status.operation_id &&
            decoded.target.partition == value.target.partition && decoded.source_filesystem_uuid == value.source_filesystem_uuid &&
            decoded.device.mounted == value.device.mounted && decoded.device.contains_data == value.device.contains_data,
        "transaction codec changed persisted state"
    );

    std::string previous_schema = document;
    const std::string current_version = "\"schemaVersion\": 8";
    const auto version = previous_schema.find(current_version);
    test_helpers::expect_true("codec schema field", version != std::string::npos, "current schema field is missing");
    if (version != std::string::npos)
        previous_schema.replace(version, current_version.size(), "\"schemaVersion\": 7");
    try {
        static_cast<void>(codec.deserialize(previous_schema));
        test_helpers::fail("codec previous schema", "the previous unreleased transaction schema was accepted");
    } catch (const btrfsbackup::ValidationError&) {
    }
}

void test_round_trip_preserves_recovery_state() {
    const auto root = test_helpers::test_root("device-preparation-transactions", "round-trip");
    DevicePreparationTransactionStore store(root);
    auto saved = transaction("prepare-round-trip", "running", now_seconds());
    saved.profile_reservation_state = "held";
    store.save(saved);
    const auto loaded = store.load_and_prune();
    test_helpers::expect_true("round trip count", loaded.transactions.size() == 1, "transaction was not loaded");
    const auto& value = loaded.transactions.front();
    test_helpers::expect_true("initial revision", value.revision == TransactionRevision{1}, "revision was not initialized");
    test_helpers::expect_eq("owner bus", value.owner.bus_name, ":1.42");
    test_helpers::expect_true("owner uid", value.owner.uid == 1000, "owner UID changed");
    test_helpers::expect_eq("stable identity", value.device.major_minor, "8:16");
    test_helpers::expect_true(
        "target mode",
        value.target.mode == btrfsbackup::provisioning::ProvisioningMode::ReformatExistingPartition,
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
    test_helpers::expect_eq("profile reservation", value.profile_reservation_state, "held");
    test_helpers::expect_eq("source filesystem UUID", value.source_filesystem_uuid, "source-btrfs-uuid");
    test_helpers::expect_eq("source mount root", value.source_mount_root, "/home");
    test_helpers::expect_eq(
        "local snapshot directory",
        value.local_snapshot_dir,
        "/home/.snapshots/btrfs-backup/test"
    );
    test_helpers::expect_eq("recovery action", value.status.recovery_action, "inspect manually");
}

void test_revision_conflicts_and_state_transitions_are_rejected() {
    const auto root = test_helpers::test_root("device-preparation-transactions", "transitions");
    DevicePreparationTransactionStore store(root);
    auto saved = transaction("prepare-transitions", "running", now_seconds());
    saved.profile_reservation_state = "held";
    store.save(saved);
    const auto stale = store.load("prepare-transitions");
    const auto changed = store.update(
        "prepare-transitions",
        stale.revision,
        [](auto& value) { value.last_completed_phase = "partition"; }
    );
    test_helpers::expect_true(
        "revision increment",
        changed.revision == TransactionRevision{2},
        "transaction revision was not incremented"
    );
    try {
        static_cast<void>(store.update(
            "prepare-transitions",
            stale.revision,
            [](auto& value) { value.luks_uuid = "stale-write"; }
        ));
        test_helpers::fail("revision conflict", "a stale transaction update was accepted");
    } catch (const btrfsbackup::ValidationError&) {
    }
    try {
        static_cast<void>(store.update(
            "prepare-transitions",
            changed.revision,
            [](auto& value) { value.status.state = "queued"; }
        ));
        test_helpers::fail("invalid state transition", "a running transaction returned to queued");
    } catch (const btrfsbackup::ValidationError&) {
    }

    auto terminal = transaction("prepare-terminal", "failed", now_seconds());
    store.save(terminal);
    try {
        static_cast<void>(store.update(
            "prepare-terminal",
            terminal.revision,
            [](auto& value) { value.status.state = "running"; }
        ));
        test_helpers::fail("terminal transition", "a terminal transaction was modified");
    } catch (const btrfsbackup::ValidationError&) {
    }
}

void test_process_updates_are_serialized_without_losing_fields() {
    const auto root = test_helpers::test_root("device-preparation-transactions", "process-update");
    DevicePreparationTransactionStore store(root);
    auto saved = transaction("prepare-process-update", "running", now_seconds());
    saved.profile_reservation_state = "held";
    store.save(saved);
    int gate[2];
    test_helpers::expect_true("process update gate", ::pipe(gate) == 0, "cannot create process update gate");
    const auto spawn_update = [&](const auto& transition) {
        const pid_t child = ::fork();
        if (child == 0) {
            ::close(gate[1]);
            char start = 0;
            if (::read(gate[0], &start, 1) != 1)
                _exit(2);
            try {
                static_cast<void>(store.update("prepare-process-update", transition));
                _exit(0);
            } catch (...) {
                _exit(3);
            }
        }
        return child;
    };
    const pid_t first = spawn_update([](auto& value) { value.partition_uuid = "partition-from-helper"; });
    const pid_t second = spawn_update([](auto& value) {
        value.cancel_requested = true;
        value.status.can_cancel = false;
    });
    ::close(gate[0]);
    const char starts[2] = {'1', '2'};
    test_helpers::expect_true("release process updates", ::write(gate[1], starts, 2) == 2, "cannot release updates");
    ::close(gate[1]);
    int first_status = 0;
    int second_status = 0;
    test_helpers::expect_true(
        "wait for process updates",
        ::waitpid(first, &first_status, 0) == first && ::waitpid(second, &second_status, 0) == second,
        "cannot wait for transaction writers"
    );
    test_helpers::expect_true(
        "process update results",
        WIFEXITED(first_status) && WEXITSTATUS(first_status) == 0 && WIFEXITED(second_status) &&
            WEXITSTATUS(second_status) == 0,
        "a serialized transaction writer failed"
    );
    const auto loaded = store.load("prepare-process-update");
    test_helpers::expect_true(
        "merged process updates",
        loaded.revision == TransactionRevision{3} && loaded.partition_uuid == "partition-from-helper" &&
            loaded.cancel_requested && loaded.status.phase == "mkfs-btrfs",
        "a concurrent field update or the helper phase was lost"
    );
}

void test_profile_reservation_is_durable_and_owner_guarded() {
    const auto root = test_helpers::test_root("device-preparation-transactions", "profile-reservation");
    DevicePreparationTransactionStore store(root);
    store.reserve_profile("archive", "prepare-first");
    test_helpers::expect_true(
        "reservation owner",
        store.profile_reservation_owner("archive") == std::optional<std::string>{"prepare-first"},
        "reservation owner was not persisted"
    );
    test_helpers::expect_true(
        "reservation survives store recreation",
        DevicePreparationTransactionStore(root).profile_reservation_owner("archive") ==
            std::optional<std::string>{"prepare-first"},
        "a new store instance did not observe the reservation"
    );

    try {
        store.reserve_profile("archive", "prepare-second");
        test_helpers::fail("reservation conflict", "a second operation reserved the same profile");
    } catch (const btrfsbackup::ValidationError&) {
    }
    try {
        store.release_profile("archive", "prepare-second");
        test_helpers::fail("reservation owner guard", "a non-owner released the reservation");
    } catch (const btrfsbackup::ValidationError&) {
    }
    store.release_profile("archive", "prepare-first");
    store.release_profile("archive", "prepare-first");
    test_helpers::expect_true(
        "reservation release",
        !store.profile_reservation_owner("archive").has_value(),
        "owner release did not remove the reservation"
    );
}

void test_round_trip_preserves_adoption_fingerprint() {
    const auto root = test_helpers::test_root("device-preparation-transactions", "adoption");
    auto value = transaction("prepare-adoption", "queued", now_seconds());
    value.target.mode = btrfsbackup::provisioning::ProvisioningMode::AdoptExistingTarget;
    value.target.expected_inspection = btrfsbackup::provisioning::ExistingTargetInspectionSummary{
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
        loaded.target.mode == btrfsbackup::provisioning::ProvisioningMode::AdoptExistingTarget &&
            loaded.target.expected_inspection == value.target.expected_inspection,
        "adoption inspection fingerprint changed during persistence"
    );
}

void test_round_trip_preserves_free_space_geometry() {
    const auto root = test_helpers::test_root("device-preparation-transactions", "free-space");
    auto value = transaction("prepare-free-space", "queued", now_seconds());
    value.target.mode =
        btrfsbackup::provisioning::ProvisioningMode::CreatePartitionInUnallocatedSpace;
    value.target.partition.reset();
    btrfsbackup::provisioning::UnallocatedRegion free_region;
    free_region.id = "ephemeral-candidate";
    free_region.start_sector = 4096;
    free_region.sector_count = 8192;
    free_region.suitable_for_backup_partition = true;
    value.target.free_region = std::move(free_region);
    value.target.planned_partition_geometry =
        btrfsbackup::provisioning::PlannedPartitionGeometry{
            .start_sector = 4096,
            .sector_count = 8192,
            .partition_number = 2,
        };
    value.partition_table_backup = "label: gpt\nlabel-id: pt-uuid\n";
    DevicePreparationTransactionStore store(root);
    store.save(value);
    const auto loaded = store.load("prepare-free-space");
    test_helpers::expect_true(
        "free-space geometry",
        loaded.target.mode ==
                btrfsbackup::provisioning::ProvisioningMode::CreatePartitionInUnallocatedSpace &&
            loaded.target.free_region.has_value() && loaded.target.free_region->id.empty() &&
            loaded.target.free_region->start_sector == 4096 && loaded.target.free_region->sector_count == 8192 &&
            loaded.target.planned_partition_geometry == value.target.planned_partition_geometry &&
            loaded.partition_table_backup == value.partition_table_backup,
        "free-space target geometry changed during persistence"
    );
}

void test_completed_limit_ttl_and_active_retention() {
    const auto root = test_helpers::test_root("device-preparation-transactions", "retention");
    DevicePreparationTransactionStore store(root, 2, std::chrono::hours(1));
    const std::int64_t now = now_seconds();
    const auto save = [&](DevicePreparationTransaction value) { store.save(value); };
    save(transaction("prepare-expired", "failed", now - 7200));
    save(transaction("prepare-oldest", "succeeded", now - 30));
    save(transaction("prepare-middle", "failed", now - 20));
    save(transaction("prepare-newest", "cancelled", now - 10));
    save(transaction("prepare-active", "running", now - 7200));
    auto held = transaction("prepare-held", "failed", now - 7200);
    held.profile_reservation_state = "held";
    store.save(held);

    const auto loaded = store.load_and_prune();
    const auto contains = [&](const std::string& id) {
        return std::ranges::find(loaded.transactions, id, [](const auto& value) {
                   return value.status.operation_id;
               }) != loaded.transactions.end();
    };
    test_helpers::expect_true("retained newest", contains("prepare-newest"), "newest result was pruned");
    test_helpers::expect_true("retained middle", contains("prepare-middle"), "second newest result was pruned");
    test_helpers::expect_true("limited oldest", !contains("prepare-oldest"), "completed limit was ignored");
    test_helpers::expect_true("expired result", !contains("prepare-expired"), "completed TTL was ignored");
    test_helpers::expect_true("active retained", contains("prepare-active"), "active transaction was TTL-pruned");
    test_helpers::expect_true("held retained", contains("prepare-held"), "held reservation transaction was TTL-pruned");
}

void test_previous_unreleased_transaction_schema_is_rejected() {
    const auto root = test_helpers::test_root("device-preparation-transactions", "legacy");
    test_helpers::write_file(
        root / "prepare-legacy.json",
        R"({
  "schemaVersion": 7,
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
        test_helpers::fail("previous transaction", "the previous unreleased transaction schema was accepted");
    } catch (const btrfsbackup::ValidationError&) {
    }
}

void test_corrupted_transaction_is_isolated_and_preserved() {
    const auto root = test_helpers::test_root("device-preparation-transactions", "corrupted");
    DevicePreparationTransactionStore store(root);
    auto valid = transaction("prepare-valid", "running", now_seconds());
    valid.profile_reservation_state = "held";
    store.save(valid);
    const auto corrupted_path = root / "prepare-corrupted.json";
    test_helpers::write_file(corrupted_path, "{not-json\n");
    std::filesystem::permissions(
        corrupted_path,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace
    );

    const auto scan = store.load_and_prune();
    test_helpers::expect_true(
        "corrupted transaction isolation",
        scan.transactions.size() == 1 && scan.transactions.front().status.operation_id == "prepare-valid" &&
            scan.corrupted_operation_ids == std::vector<std::string>{"prepare-corrupted"},
        "a corrupted record blocked or contaminated the valid transaction"
    );
    test_helpers::expect_true(
        "corrupted transaction preserved",
        std::filesystem::exists(corrupted_path),
        "the corrupted transaction was removed instead of retained for diagnostics"
    );
}

} // namespace

int main() {
    test_codec_owns_schema_and_validation();
    test_round_trip_preserves_recovery_state();
    test_revision_conflicts_and_state_transitions_are_rejected();
    test_process_updates_are_serialized_without_losing_fields();
    test_profile_reservation_is_durable_and_owner_guarded();
    test_round_trip_preserves_adoption_fingerprint();
    test_round_trip_preserves_free_space_geometry();
    test_completed_limit_ttl_and_active_retention();
    test_previous_unreleased_transaction_schema_is_rejected();
    test_corrupted_transaction_is_isolated_and_preserved();
    return test_helpers::finish("device preparation transaction tests");
}
