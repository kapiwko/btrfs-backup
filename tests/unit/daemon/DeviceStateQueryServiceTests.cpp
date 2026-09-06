// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <config/json/JsonIo.hpp>
#include <core/Errors.hpp>
#include <daemon/query/DeviceStateQueryService.hpp>
#include <state/persistence/FileTargetStorageMeasurementStore.hpp>

#include "support/ValidationTestHelpers.hpp"

namespace fs = std::filesystem;

namespace {

constexpr const char* target_uuid = "66666666-7777-8888-9999-aaaaaaaaaaaa";
constexpr const char* replacement_uuid = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee";

btrfsbackup::config::json::Json profile_document(
    const std::string& btrfs_uuid,
    const fs::path& device = "/dev/null"
) {
    return {
        {"schemaVersion", 1},
        {"configurationGeneration", "0123456789abcdef0123456789abcdef"},
        {"profileId", "default"},
        {"name", "Default backup"},
        {"enabled", true},
        {"target", {
                       {"device", device.string()},
                       {"luksUuid", "11111111-2222-3333-4444-555555555555"},
                       {"btrfsUuid", btrfs_uuid},
                       {"mapperName", "backupdisk"},
                       {"activation", {{"mode", "askPassword"}}},
                   }},
        {"sources", btrfsbackup::config::json::Json::array({{
                        {"id", "home"},
                        {"name", "Home"},
                        {"enabled", true},
                        {"subvolume", "/home"},
                        {"localSnapshotDir", "/.snapshots/home"},
                        {"remoteSubdir", "home"},
                        {"remoteRetention", 2},
                        {"localRetention", 2},
                    }})},
        {"settings", {
                         {"dailyLimit", true},
                         {"incrementalRequired", true},
                         {"keepFailedLocalSnapshot", false},
                         {"autoEject", true},
                         {"remoteRetention", 2},
                         {"localRetention", 2},
                         {"minimumTargetFreeBytes", 500},
                         {"minimumLocalFreeBytes", 0},
                     }},
    };
}

btrfsbackup::daemon::ManagerPaths manager_paths(const fs::path& root) {
    return {
        .config_root = root / "etc",
        .public_profile_root = root / "public",
        .status_root = root / "status",
        .history_root = root / "history",
        .state_root = root / "state",
        .target_mount_root = root / "mnt",
        .mapper_root = root / "mapper",
        .mountinfo_path = root / "unused-mountinfo",
    };
}

btrfsbackup::backup::TargetStorageMeasurement measurement(
    std::uint64_t capacity = 2000,
    std::uint64_t free = 1000,
    std::uint64_t available = 900
) {
    return {
        .space = {capacity, free, available},
        .measured_at = *btrfsbackup::parse_utc_timestamp("2026-08-30T12:34:56Z"),
    };
}

struct MountReader final : btrfsbackup::backup::IMountInspector {
    std::vector<btrfsbackup::backup::MountEntry> entries;
    mutable int reads = 0;

    std::vector<btrfsbackup::backup::MountEntry> inspect() const override {
        ++reads;
        return entries;
    }
};

struct SpaceProbe final : btrfsbackup::backup::IFilesystemSpaceProbe {
    btrfsbackup::backup::FilesystemSpace result{1000, 400, 350};
    bool fail = false;
    mutable int reads = 0;

    btrfsbackup::backup::FilesystemSpace measure_verified_mount(
        const fs::path&,
        const btrfsbackup::backup::MountEntry&
    ) const override {
        ++reads;
        if (fail) {
            throw btrfsbackup::ValidationError("space probe failed");
        }
        return result;
    }
};

struct CacheReader final : btrfsbackup::backup::ITargetStorageMeasurementReader {
    std::optional<btrfsbackup::backup::TargetStorageMeasurement> value;
    mutable int reads = 0;

    std::optional<btrfsbackup::backup::TargetStorageMeasurement> read_matching(
        const btrfsbackup::config::Profile&
    ) const override {
        ++reads;
        return value;
    }
};

struct Fixture {
    explicit Fixture(const std::string& name)
        : root(test_helpers::test_root("device-state-query-service", name)), paths(manager_paths(root)) {
        write_profile(target_uuid);
    }

    ~Fixture() {
        fs::remove_all(root);
    }

    void write_profile(const std::string& uuid, const fs::path& device = "/dev/null") const {
        test_helpers::write_file(
            root / "etc" / "profiles" / "default" / "profile.json",
            btrfsbackup::config::json::dump_json(profile_document(uuid, device))
        );
    }

    btrfsbackup::backup::MountEntry valid_mount() const {
        return {
            .source = (paths.mapper_root / "backupdisk").string(),
            .target = (paths.target_mount_root / "default").string(),
            .fstype = "btrfs",
            .options = "rw,nodev,nosuid,noexec,nosymfollow",
            .mount_id = 42,
            .filesystem_uuid = target_uuid,
        };
    }

    btrfsbackup::daemon::TargetStatus query() {
        const btrfsbackup::daemon::query::DeviceStateQueryService service(
            paths,
            mounts,
            probe,
            cache
        );
        return service.get_device_state("default");
    }

    fs::path root;
    btrfsbackup::daemon::ManagerPaths paths;
    MountReader mounts;
    SpaceProbe probe;
    CacheReader cache;
};

void test_verified_mount_uses_live_measurement() {
    Fixture fixture("live");
    fixture.mounts.entries = {fixture.valid_mount()};
    fixture.cache.value = measurement(2000, 1000, 900);

    const btrfsbackup::daemon::TargetStatus status = fixture.query();

    test_helpers::expect_eq("live state", status.state, "mounted");
    test_helpers::expect_true("verified target mounted", status.mounted, "verified target was not mounted");
    test_helpers::expect_true(
        "live measurement selected",
        status.storage.has_value() && status.storage->live && status.storage->capacity_bytes == 1000,
        "cache replaced a valid live measurement"
    );
    test_helpers::expect_true(
        "live configured minimum",
        status.storage.has_value() &&
            status.storage->space_state == btrfsbackup::state::document::TargetSpaceState::BelowConfiguredMinimum,
        "live measurement ignored the configured minimum"
    );
    test_helpers::expect_true("live avoids cache", fixture.cache.reads == 0, "cache was read after a live measurement");
}

void test_foreign_filesystem_uses_only_cached_measurement() {
    Fixture fixture("foreign-filesystem");
    fixture.mounts.entries = {{
        .source = "tmpfs",
        .target = (fixture.paths.target_mount_root / "default").string(),
        .fstype = "tmpfs",
        .mount_id = 43,
    }};
    fixture.cache.value = measurement();

    const btrfsbackup::daemon::TargetStatus status = fixture.query();

    test_helpers::expect_eq("foreign filesystem state", status.state, "unexpected-mount");
    test_helpers::expect_true("foreign filesystem not mounted", !status.mounted, "foreign filesystem was accepted");
    test_helpers::expect_true("foreign filesystem not probed", fixture.probe.reads == 0, "foreign filesystem was measured");
    test_helpers::expect_true(
        "foreign filesystem cache",
        status.storage.has_value() && !status.storage->live && status.storage->capacity_bytes == 2000,
        "matching cache was not used"
    );
}

void test_matching_uuid_with_wrong_mapper_is_rejected() {
    Fixture fixture("wrong-mapper");
    btrfsbackup::backup::MountEntry mount = fixture.valid_mount();
    mount.source = "/dev/mapper/cloned-target";
    fixture.mounts.entries = {mount};

    const btrfsbackup::daemon::TargetStatus status = fixture.query();

    test_helpers::expect_eq("wrong mapper state", status.state, "unexpected-mount");
    test_helpers::expect_true("wrong mapper not mounted", !status.mounted, "cloned target was accepted");
    test_helpers::expect_true("wrong mapper not probed", fixture.probe.reads == 0, "cloned target was measured");
}

void test_probe_failure_falls_back_to_cache() {
    Fixture fixture("probe-failure");
    fixture.mounts.entries = {fixture.valid_mount()};
    fixture.probe.fail = true;
    fixture.cache.value = measurement();

    const btrfsbackup::daemon::TargetStatus status = fixture.query();

    test_helpers::expect_true("failed probe attempted", fixture.probe.reads == 1, "live probe was not attempted");
    test_helpers::expect_true("failed probe reads cache", fixture.cache.reads == 1, "cache fallback was not attempted");
    test_helpers::expect_true(
        "failed probe cached result",
        status.storage.has_value() && !status.storage->live && status.storage->capacity_bytes == 2000,
        "probe failure did not return cached storage"
    );
}

void test_target_uuid_change_invalidates_cache() {
    Fixture fixture("changed-uuid");
    test_helpers::write_file(
        fixture.paths.state_root / "profiles" / "default" / "target-storage.json",
        R"({"schemaVersion":1,"profileId":"default","targetIdentity":{"luksUuid":"11111111-2222-3333-4444-555555555555","btrfsUuid":"66666666-7777-8888-9999-aaaaaaaaaaaa","partitionUuid":""},"measurement":{"capacityBytes":2000,"freeBytes":1000,"availableBytes":900,"measuredAt":"2026-08-30T12:34:56Z"}})"
    );
    btrfsbackup::state::FileTargetStorageMeasurementStore cache(fixture.paths.state_root);
    const btrfsbackup::daemon::query::DeviceStateQueryService service(
        fixture.paths,
        fixture.mounts,
        fixture.probe,
        cache
    );
    test_helpers::expect_true(
        "initial cache",
        service.get_device_state("default").storage.has_value(),
        "matching cache was unavailable"
    );

    fixture.write_profile(replacement_uuid);
    const btrfsbackup::daemon::TargetStatus status = service.get_device_state("default");

    test_helpers::expect_true("changed UUID cache", !status.storage.has_value(), "cache survived a target UUID change");
}

void test_missing_cache_leaves_storage_unknown() {
    Fixture fixture("missing-cache");

    const btrfsbackup::daemon::TargetStatus status = fixture.query();

    test_helpers::expect_true("missing cache", !status.storage.has_value(), "missing cache produced storage data");
}

void test_corrupt_cache_leaves_storage_unknown() {
    Fixture fixture("corrupt-cache");
    test_helpers::write_file(
        fixture.paths.state_root / "profiles" / "default" / "target-storage.json",
        "{invalid"
    );
    btrfsbackup::state::FileTargetStorageMeasurementStore cache(fixture.paths.state_root);
    const btrfsbackup::daemon::query::DeviceStateQueryService service(
        fixture.paths,
        fixture.mounts,
        fixture.probe,
        cache
    );

    const btrfsbackup::daemon::TargetStatus status = service.get_device_state("default");

    test_helpers::expect_true("corrupt cache", !status.storage.has_value(), "corrupt cache produced storage data");
}

void test_query_uses_only_observation_ports() {
    Fixture fixture("observation-only");

    const btrfsbackup::daemon::TargetStatus status = fixture.query();

    test_helpers::expect_eq("observation-only state", status.state, "connected");
    test_helpers::expect_true("observation-only safe removal", status.safe_to_remove, "closed target was not safe to remove");
    test_helpers::expect_true("single mount read", fixture.mounts.reads == 1, "mount state was not read exactly once");
    test_helpers::expect_true("single cache read", fixture.cache.reads == 1, "cache was not read exactly once");
    test_helpers::expect_true("no unverified probe", fixture.probe.reads == 0, "unmounted target was probed");
}

void test_disconnected_device_is_not_reported_as_unlocked_by_stale_mapper() {
    Fixture fixture("disconnected-stale-mapper");
    fixture.write_profile(target_uuid, "/dev/nonexistent-btrfsbackup-test-device");
    test_helpers::write_file(fixture.paths.mapper_root / "backupdisk", "stale mapper");

    const btrfsbackup::daemon::TargetStatus status = fixture.query();

    test_helpers::expect_eq("stale mapper disconnected state", status.state, "disconnected");
    test_helpers::expect_true(
        "stale mapper device disconnected",
        !status.connected,
        "missing physical device was reported as connected"
    );
    test_helpers::expect_true(
        "stale mapper not unlocked",
        !status.unlocked,
        "stale mapper was presented as an unlocked target"
    );
}

} // namespace

int main() {
    test_verified_mount_uses_live_measurement();
    test_foreign_filesystem_uses_only_cached_measurement();
    test_matching_uuid_with_wrong_mapper_is_rejected();
    test_probe_failure_falls_back_to_cache();
    test_target_uuid_change_invalidates_cache();
    test_missing_cache_leaves_storage_unknown();
    test_corrupt_cache_leaves_storage_unknown();
    test_query_uses_only_observation_ports();
    test_disconnected_device_is_not_reported_as_unlocked_by_stale_mapper();
    return test_helpers::finish("device state query service tests");
}
