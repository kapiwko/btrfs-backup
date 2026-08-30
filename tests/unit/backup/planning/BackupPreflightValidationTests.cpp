// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include <backup/planning/BackupPreflightValidation.hpp>

#include "support/ValidationTestHelpers.hpp"

namespace fs = std::filesystem;

namespace {

btrfsbackup::config::Profile profile(const fs::path& mount_point = "/mnt/backup") {
    btrfsbackup::config::Profile result{
        btrfsbackup::ProfileId{"default"},
        {
            btrfsbackup::config::LuksUuid{"11111111-2222-3333-4444-555555555555"},
            btrfsbackup::config::BtrfsUuid{"22222222-3333-4444-5555-666666666666"},
            btrfsbackup::config::PartitionUuid{""},
            btrfsbackup::config::MapperName{"backup"},
        },
        {
            btrfsbackup::config::RemoteSnapshotRoot{(mount_point / "snapshots").string()},
            btrfsbackup::config::IncomingRoot{(mount_point / ".incoming").string()},
        },
    };
    result.target.mount_point = btrfsbackup::config::TargetMountPoint{mount_point};
    return result;
}

std::vector<btrfsbackup::backup::MountEntry> mounts(const fs::path& mount_point = "/mnt/backup") {
    return {
        {
            .source = "/dev/mapper/backup",
            .target = mount_point.string(),
            .fstype = "btrfs",
            .options = "rw,relatime,nodev,nosuid,noexec,nosymfollow",
            .device_id = "0:21",
            .filesystem_uuid = "22222222-3333-4444-5555-666666666666",
        },
    };
}

btrfsbackup::config::Profile profile_with_source() {
    btrfsbackup::config::Profile result = profile();
    btrfsbackup::config::ProfileSource source{btrfsbackup::SourceId{"home"}};
    source.subvolume = btrfsbackup::config::SourceSubvolumePath{"/home/live"};
    source.local_snapshot_dir = btrfsbackup::config::LocalSnapshotRoot{"/home/snapshots"};
    result.sources.push_back(std::move(source));
    return result;
}

std::vector<btrfsbackup::backup::MountEntry> mounts_with_source() {
    std::vector<btrfsbackup::backup::MountEntry> result = mounts();
    result.push_back({
        .source = "/dev/source",
        .target = "/home",
        .fstype = "btrfs",
        .options = "rw",
        .device_id = "0:22",
        .filesystem_uuid = "source-fs",
    });
    return result;
}

void test_accepts_valid_target_mount() {
    const btrfsbackup::backup::MountEntry verified =
        btrfsbackup::backup::planning::validate_backup_mounts(profile(), mounts());
    test_helpers::expect_eq(
        "verified target source",
        verified.source,
        "/dev/mapper/backup"
    );
}

void test_rejects_missing_mount() {
    test_helpers::expect_validation_error("missing target mount", [] { btrfsbackup::backup::planning::validate_backup_mounts(profile(), {}); }, "not mounted");
}

void test_rejects_wrong_filesystem_type() {
    std::vector<btrfsbackup::backup::MountEntry> entries = mounts();
    entries.at(0).fstype = "ext4";

    test_helpers::expect_validation_error("wrong filesystem", [&] { btrfsbackup::backup::planning::validate_backup_mounts(profile(), entries); }, "not a Btrfs filesystem");
}

void test_rejects_wrong_mapper() {
    std::vector<btrfsbackup::backup::MountEntry> entries = mounts();
    entries.at(0).source = "/dev/mapper/other";

    test_helpers::expect_validation_error("wrong mapper", [&] { btrfsbackup::backup::planning::validate_backup_mounts(profile(), entries); }, "is not /dev/mapper/backup");
}

void test_rejects_read_only_mount() {
    std::vector<btrfsbackup::backup::MountEntry> entries = mounts();
    entries.at(0).options = "ro,relatime";

    test_helpers::expect_validation_error("read-only target", [&] { btrfsbackup::backup::planning::validate_backup_mounts(profile(), entries); }, "not mounted read-write");
}

void test_rejects_missing_security_mount_options() {
    for (const std::string missing : {"nodev", "nosuid", "noexec", "nosymfollow"}) {
        std::vector<btrfsbackup::backup::MountEntry> entries = mounts();
        entries.at(0).options = "rw,relatime";
        for (const std::string option : {"nodev", "nosuid", "noexec", "nosymfollow"}) {
            if (option != missing) {
                entries.at(0).options += "," + option;
            }
        }

        test_helpers::expect_validation_error("missing " + missing, [&] { btrfsbackup::backup::planning::validate_backup_mounts(profile(), entries); }, "missing required mount option " + missing);
    }
}

void test_rejects_uuid_mismatch() {
    std::vector<btrfsbackup::backup::MountEntry> entries = mounts();
    entries.at(0).filesystem_uuid = "other-fs";

    test_helpers::expect_validation_error("uuid mismatch", [&] { btrfsbackup::backup::planning::validate_backup_mounts(profile(), entries); }, "Btrfs UUID mismatch");
}

void test_rejects_remote_root_symlink_escape() {
    fs::path root = test_helpers::test_root("backup-preflight-validation", "remote-escape");
    fs::path mount_point = root / "mnt" / "backup";
    fs::path outside = root / "outside";
    fs::create_directories(mount_point);
    fs::create_directories(outside);
    fs::create_directory_symlink(outside, mount_point / "escape");

    btrfsbackup::config::Profile test_profile = profile(mount_point);
    test_profile.paths.remote_root = btrfsbackup::config::RemoteSnapshotRoot{(mount_point / "escape" / "snapshots").string()};

    test_helpers::expect_validation_error("remote root escape", [&] { btrfsbackup::backup::planning::validate_backup_mounts(test_profile, mounts(mount_point)); }, "REMOTE_ROOT escapes");

    fs::remove_all(root);
}

void test_rejects_incoming_root_symlink_escape() {
    fs::path root = test_helpers::test_root("backup-preflight-validation", "incoming-escape");
    fs::path mount_point = root / "mnt" / "backup";
    fs::path outside = root / "outside";
    fs::create_directories(mount_point);
    fs::create_directories(outside);
    fs::create_directory_symlink(outside, mount_point / "escape");

    btrfsbackup::config::Profile test_profile = profile(mount_point);
    test_profile.paths.incoming_root = btrfsbackup::config::IncomingRoot{(mount_point / "escape" / ".incoming").string()};

    test_helpers::expect_validation_error("incoming root escape", [&] { btrfsbackup::backup::planning::validate_backup_mounts(test_profile, mounts(mount_point)); }, "INCOMING_ROOT escapes");

    fs::remove_all(root);
}

void test_rejects_local_snapshot_directory_on_another_filesystem() {
    btrfsbackup::config::Profile test_profile = profile_with_source();
    test_profile.sources.front().local_snapshot_dir = btrfsbackup::config::LocalSnapshotRoot{"/snapshots/home"};
    std::vector<btrfsbackup::backup::MountEntry> entries = mounts_with_source();
    entries.push_back({
        .source = "/dev/other",
        .target = "/snapshots",
        .fstype = "btrfs",
        .options = "rw",
        .device_id = "0:23",
        .filesystem_uuid = "other-fs",
    });

    test_helpers::expect_validation_error(
        "local snapshot filesystem",
        [&] { btrfsbackup::backup::planning::validate_backup_mounts(test_profile, entries); },
        "LOCAL_SNAPSHOT_DIR must be on the same Btrfs filesystem"
    );
}

void test_rejects_source_on_backup_target_filesystem() {
    btrfsbackup::config::Profile test_profile = profile_with_source();
    test_profile.sources.front().subvolume = btrfsbackup::config::SourceSubvolumePath{"/mnt/backup/live"};
    test_profile.sources.front().local_snapshot_dir = btrfsbackup::config::LocalSnapshotRoot{"/mnt/backup/local"};

    test_helpers::expect_validation_error(
        "source on target filesystem",
        [&] { btrfsbackup::backup::planning::validate_backup_mounts(test_profile, mounts()); },
        "SOURCE_SUBVOLUME must not be on the backup target filesystem"
    );
}

} // namespace

int main() {
    test_accepts_valid_target_mount();
    test_rejects_missing_mount();
    test_rejects_wrong_filesystem_type();
    test_rejects_wrong_mapper();
    test_rejects_read_only_mount();
    test_rejects_missing_security_mount_options();
    test_rejects_uuid_mismatch();
    test_rejects_remote_root_symlink_escape();
    test_rejects_incoming_root_symlink_escape();
    test_rejects_local_snapshot_directory_on_another_filesystem();
    test_rejects_source_on_backup_target_filesystem();

    return test_helpers::finish("backup preflight validation tests");
}
