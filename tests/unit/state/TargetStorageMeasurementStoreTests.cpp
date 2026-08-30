// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>

#include <core/RuntimeTime.hpp>
#include <platform/linux/filesystem/PosixDurableFileOperations.hpp>
#include <state/persistence/FileTargetStorageMeasurementStore.hpp>

#include "support/TestHelpers.hpp"

namespace fs = std::filesystem;

namespace {

btrfsbackup::config::Profile profile() {
    return {
        btrfsbackup::ProfileId{"default"},
        {
            btrfsbackup::config::LuksUuid{"11111111-2222-3333-4444-555555555555"},
            btrfsbackup::config::BtrfsUuid{"22222222-3333-4444-5555-666666666666"},
            btrfsbackup::config::PartitionUuid{"33333333-4444-5555-6666-777777777777"},
            btrfsbackup::config::MapperName{"backup"},
        },
        {
            btrfsbackup::config::RemoteSnapshotRoot{"/mnt/backup/snapshots"},
            btrfsbackup::config::IncomingRoot{"/mnt/backup/.incoming"},
        },
    };
}

void test_round_trip_and_identity_binding() {
    const fs::path root = test_helpers::test_root("target-storage-store", "round-trip");
    btrfsbackup::platform::linux::filesystem::PosixDurableFileOperations files;
    btrfsbackup::state::FileTargetStorageMeasurementStore store(root, &files);
    const auto timestamp = *btrfsbackup::parse_utc_timestamp("2026-08-30T12:34:56Z");
    const btrfsbackup::config::Profile expected = profile();
    store.write(expected, {{1000, 400, 350}, timestamp});

    const auto loaded = store.read_matching(expected);
    test_helpers::expect_true("measurement loaded", loaded.has_value(), "stored measurement was not loaded");
    test_helpers::expect_true("capacity preserved", loaded.has_value() && loaded->space.capacity_bytes == 1000, "capacity changed");
    test_helpers::expect_true("used derived", loaded.has_value() && loaded->space.used_bytes() == 600, "used bytes are wrong");
    test_helpers::expect_true("timestamp preserved", loaded.has_value() && loaded->measured_at == timestamp, "timestamp changed");

    btrfsbackup::config::Profile changed = expected;
    changed.target.btrfs_uuid = btrfsbackup::config::BtrfsUuid{"AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE"};
    test_helpers::expect_true("identity mismatch ignored", !store.read_matching(changed).has_value(), "cache survived target identity change");

    const fs::perms permissions = fs::status(root / "profiles" / "default" / "target-storage.json").permissions();
    test_helpers::expect_true(
        "private permissions",
        (permissions & (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none,
        "measurement document is not private"
    );
    fs::remove_all(root);
}

void test_invalid_document_is_ignored() {
    const fs::path root = test_helpers::test_root("target-storage-store", "invalid");
    btrfsbackup::platform::linux::filesystem::PosixDurableFileOperations files;
    btrfsbackup::state::FileTargetStorageMeasurementStore store(root, &files);
    const btrfsbackup::config::Profile expected = profile();
    files.ensure_directory(root / "profiles" / "default", fs::perms::owner_all);
    files.write_atomically(root / "profiles" / "default" / "target-storage.json", "{\"schemaVersion\":2}", fs::perms::owner_read | fs::perms::owner_write);
    test_helpers::expect_true("unsupported schema ignored", !store.read_matching(expected).has_value(), "unsupported cache was accepted");
    fs::remove_all(root);
}

} // namespace

int main() {
    test_round_trip_and_identity_binding();
    test_invalid_document_is_ignored();
    return test_helpers::finish("target storage measurement store tests");
}
