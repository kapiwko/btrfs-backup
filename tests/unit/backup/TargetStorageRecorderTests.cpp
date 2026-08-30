// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <backup/TargetStorageRecorder.hpp>

#include <core/Errors.hpp>

#include "support/TestHelpers.hpp"

namespace {

struct Mounts final : btrfsbackup::backup::IMountInspector {
    std::vector<btrfsbackup::backup::MountEntry> entries;

    std::vector<btrfsbackup::backup::MountEntry> inspect() const override {
        return entries;
    }
};

struct Probe final : btrfsbackup::backup::IFilesystemSpaceProbe {
    bool fail = false;
    mutable int calls = 0;

    btrfsbackup::backup::FilesystemSpace measure_verified_mount(
        const std::filesystem::path&,
        const btrfsbackup::backup::MountEntry&
    ) const override {
        ++calls;
        if (fail) {
            throw btrfsbackup::ValidationError("space probe failed");
        }
        return {1000, 400, 350};
    }
};

struct Store final : btrfsbackup::backup::ITargetStorageMeasurementStore {
    int writes = 0;
    std::optional<btrfsbackup::backup::TargetStorageMeasurement> measurement;

    void write(
        const btrfsbackup::config::Profile&,
        const btrfsbackup::backup::TargetStorageMeasurement& value
    ) override {
        ++writes;
        measurement = value;
    }

    std::optional<btrfsbackup::backup::TargetStorageMeasurement> read_matching(
        const btrfsbackup::config::Profile&
    ) const override {
        return measurement;
    }
};

struct Clock final : btrfsbackup::backup::IClock {
    btrfsbackup::RuntimeTimePoint timestamp = *btrfsbackup::parse_utc_timestamp("2026-08-30T12:34:56Z");

    btrfsbackup::RuntimeTimePoint now() const override {
        return timestamp;
    }

    btrfsbackup::LocalDate local_date() const override {
        return btrfsbackup::local_date_at(timestamp);
    }
};

btrfsbackup::config::Profile profile() {
    btrfsbackup::config::Profile result{
        btrfsbackup::ProfileId{"default"},
        {
            btrfsbackup::config::LuksUuid{"11111111-2222-3333-4444-555555555555"},
            btrfsbackup::config::BtrfsUuid{"22222222-3333-4444-5555-666666666666"},
            btrfsbackup::config::PartitionUuid{""},
            btrfsbackup::config::MapperName{"backup"},
        },
        {
            btrfsbackup::config::RemoteSnapshotRoot{"/mnt/backup/snapshots"},
            btrfsbackup::config::IncomingRoot{"/mnt/backup/.incoming"},
        },
    };
    result.target.mount_point = btrfsbackup::config::TargetMountPoint{"/mnt/backup"};
    return result;
}

void test_records_only_verified_target_mount() {
    const btrfsbackup::config::Profile expected = profile();
    Mounts mounts;
    mounts.entries = {{
        .source = "/dev/mapper/backup",
        .target = "/mnt/backup",
        .fstype = "btrfs",
        .mount_id = 42,
        .filesystem_uuid = expected.target.btrfs_uuid.value(),
    }};
    Probe probe;
    Store store;
    Clock clock;
    btrfsbackup::backup::TargetStorageRecorder recorder(mounts, probe, store, clock);

    const auto warning = recorder.record(expected);
    test_helpers::expect_true("record succeeds", !warning.has_value(), "valid measurement produced a warning");
    test_helpers::expect_true("measurement written", store.writes == 1, "measurement was not persisted");
    test_helpers::expect_true(
        "measurement timestamp",
        store.measurement.has_value() && store.measurement->measured_at == clock.timestamp,
        "measurement timestamp is wrong"
    );

    mounts.entries.front().filesystem_uuid = "AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE";
    (void)recorder.record(expected);
    test_helpers::expect_true("mismatch ignored", store.writes == 1, "identity mismatch overwrote the cache");
}

void test_probe_failure_becomes_warning() {
    const btrfsbackup::config::Profile expected = profile();
    Mounts mounts;
    mounts.entries = {{
        .target = "/mnt/backup",
        .fstype = "btrfs",
        .mount_id = 42,
        .filesystem_uuid = expected.target.btrfs_uuid.value(),
    }};
    Probe probe;
    probe.fail = true;
    Store store;
    Clock clock;
    btrfsbackup::backup::TargetStorageRecorder recorder(mounts, probe, store, clock);

    const auto warning = recorder.record(expected);
    test_helpers::expect_true("probe warning", warning.has_value(), "probe failure escaped as success");
    test_helpers::expect_true("failed probe not stored", store.writes == 0, "failed measurement was persisted");
}

} // namespace

int main() {
    test_records_only_verified_target_mount();
    test_probe_failure_becomes_warning();
    return test_helpers::finish("target storage recorder tests");
}
