// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include <daemon/provisioning/ConfiguredBackupTargetMarker.hpp>

#include <algorithm>

#include "support/TestHelpers.hpp"

namespace {

btrfsbackup::daemon::provisioning::ExistingPartition partition(
    std::string path,
    std::string partition_uuid,
    std::string filesystem_type,
    std::string filesystem_uuid
) {
    btrfsbackup::daemon::provisioning::ExistingPartition result;
    result.identity.display_path = std::move(path);
    result.partition_uuid = std::move(partition_uuid);
    result.filesystem.type = std::move(filesystem_type);
    result.filesystem.uuid = std::move(filesystem_uuid);
    result.suitable_for_reformat = true;
    result.suitable_for_adoption = true;
    return result;
}

void test_marks_targets_by_partition_or_luks_uuid() {
    namespace provisioning = btrfsbackup::daemon::provisioning;
    provisioning::StorageDevice device;
    device.regions.emplace_back(partition("/dev/test1", "part-1", "ext4", "filesystem-1"));
    device.regions.emplace_back(partition("/dev/test2", "part-2", "crypto_LUKS", "luks-2"));
    device.regions.emplace_back(partition("/dev/test3", "part-3", "crypto_LUKS", "luks-3"));
    provisioning::StorageTopology topology{.devices = {std::move(device)}};

    provisioning::ConfiguredBackupTargetMarker({
                                                   {.partition_uuid = "part-1", .luks_uuid = "different-luks"},
                                                   {.partition_uuid = "different-part", .luks_uuid = "luks-2"},
                                               })
        .apply(topology);

    const auto& first = std::get<provisioning::ExistingPartition>(topology.devices[0].regions[0]);
    const auto& second = std::get<provisioning::ExistingPartition>(topology.devices[0].regions[1]);
    const auto& third = std::get<provisioning::ExistingPartition>(topology.devices[0].regions[2]);
    const auto marked = [](const auto& candidate) {
        return candidate.configured_backup_target && !candidate.suitable_for_reformat &&
            !candidate.suitable_for_adoption &&
            std::ranges::find(
                candidate.blockers,
                provisioning::SafetyBlocker{"configured-backup-target", candidate.identity.display_path}
            ) != candidate.blockers.end();
    };
    test_helpers::expect_true(
        "configured target partition UUID",
        marked(first),
        "PARTUUID match was not blocked"
    );
    test_helpers::expect_true(
        "configured target LUKS UUID",
        marked(second),
        "LUKS UUID match was not blocked"
    );
    test_helpers::expect_true(
        "unconfigured partition remains available",
        !third.configured_backup_target && third.suitable_for_reformat && third.suitable_for_adoption &&
            third.blockers.empty(),
        "unrelated partition was blocked"
    );
}

void test_ignores_empty_identities_and_does_not_duplicate_blocker() {
    namespace provisioning = btrfsbackup::daemon::provisioning;
    auto candidate = partition("/dev/test1", "part-1", "crypto_LUKS", "luks-1");
    candidate.blockers.push_back({"configured-backup-target", "/dev/test1"});
    provisioning::StorageDevice device;
    device.regions.emplace_back(std::move(candidate));
    provisioning::StorageTopology topology{.devices = {std::move(device)}};

    provisioning::ConfiguredBackupTargetMarker({
                                                   {.partition_uuid = "", .luks_uuid = ""},
                                                   {.partition_uuid = "part-1", .luks_uuid = "luks-1"},
                                               })
        .apply(topology);

    const auto& marked = std::get<provisioning::ExistingPartition>(topology.devices[0].regions[0]);
    test_helpers::expect_true(
        "configured target blocker is unique",
        std::ranges::count(
            marked.blockers,
            provisioning::SafetyBlocker{"configured-backup-target", "/dev/test1"}
        ) == 1,
        "configured target blocker was duplicated"
    );
}

} // namespace

int main() {
    test_marks_targets_by_partition_or_luks_uuid();
    test_ignores_empty_identities_and_does_not_duplicate_blocker();
    return test_helpers::finish("configured backup target marker tests");
}
