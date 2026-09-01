// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/control/DevicePreparationTransaction.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <ranges>

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
    test_helpers::expect_eq("last completed phase", value.last_completed_phase, "luks-format");
    test_helpers::expect_eq("partition", value.partition, "/dev/test1");
    test_helpers::expect_eq("LUKS UUID", value.luks_uuid, "luks-uuid");
    test_helpers::expect_eq("mapper", value.mapper, "btrfs-backup-test");
    test_helpers::expect_eq("recovery action", value.status.recovery_action, "inspect manually");
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

} // namespace

int main() {
    test_round_trip_preserves_recovery_state();
    test_completed_limit_ttl_and_active_retention();
    return test_helpers::finish("device preparation transaction tests");
}
