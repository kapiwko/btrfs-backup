// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <string>

#include <platform/linux/mount_info.hpp>

#include "support/validation_test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

void test_reads_mount_table() {
    fs::path root = test_helpers::test_root("mount-info", "read");
    fs::path mountinfo = root / "mountinfo";
    test_helpers::write_file(
        mountinfo,
        "21 31 0:20 / / rw,relatime - btrfs /dev/sda2 rw,subvolid=5\n"
        "22 21 0:20 /@home /home rw,relatime - btrfs /dev/sda2 rw,subvol=/@home\n"
        "23 21 0:22 / /run rw,nosuid,nodev - tmpfs tmpfs rw,size=123\n"
        "24 21 0:21 / /mnt/backup rw,relatime - btrfs /dev/mapper/backupdisk[/snapshots] rw,subvolid=5\n"
    );

    std::vector<btrfsbackup::backup::MountEntry> entries = btrfsbackup::platform::linux::read_mount_table(mountinfo, [](const std::string& source) {
        if (source == "/dev/sda2") {
            return std::string{"source-uuid"};
        }
        if (source == "/dev/mapper/backupdisk") {
            return std::string{"target-uuid"};
        }
        return std::string{};
    });
    test_helpers::expect_eq("mount entry count", std::to_string(entries.size()), "4");
    test_helpers::expect_eq("mount first target", entries.at(0).target, "/");
    test_helpers::expect_eq("mount first fstype", entries.at(0).fstype, "btrfs");
    test_helpers::expect_eq("mount first root", entries.at(0).root, "/");
    test_helpers::expect_eq("mount first options", entries.at(0).options, "rw,relatime,subvolid=5");
    test_helpers::expect_eq("mount first device id", entries.at(0).device_id, "0:20");
    test_helpers::expect_eq("mount first filesystem uuid", entries.at(0).filesystem_uuid, "source-uuid");
    test_helpers::expect_eq("mount tmpfs source", entries.at(2).source, "tmpfs");
    test_helpers::expect_eq("mount tmpfs filesystem uuid", entries.at(2).filesystem_uuid, "");
    test_helpers::expect_eq("mount backup filesystem uuid", entries.at(3).filesystem_uuid, "target-uuid");

    std::vector<std::string> targets = btrfsbackup::platform::linux::btrfs_mount_targets(mountinfo);
    test_helpers::expect_eq("btrfs target count", std::to_string(targets.size()), "3");
    test_helpers::expect_eq("btrfs target root", targets.at(0), "/");
    test_helpers::expect_eq("btrfs target home", targets.at(1), "/home");
    test_helpers::expect_eq("btrfs target backup", targets.at(2), "/mnt/backup");

    fs::remove_all(root);
}

void test_mount_lookup_and_matching() {
    std::vector<btrfsbackup::backup::MountEntry> entries{
        {
            .source = "/dev/sda2",
            .target = "/",
            .fstype = "btrfs",
            .device_id = "0:20",
            .filesystem_uuid = "source-uuid",
        },
        {
            .source = "/dev/sda2",
            .target = "/home",
            .fstype = "btrfs",
            .device_id = "0:20",
            .filesystem_uuid = "source-uuid",
        },
        {
            .source = "/dev/mapper/backupdisk[/snapshots]",
            .target = "/mnt/backup",
            .fstype = "btrfs",
            .device_id = "0:21",
            .filesystem_uuid = "target-uuid",
        },
    };

    auto home_mount = btrfsbackup::backup::mount_for_path(entries, "/home/user/file");
    test_helpers::expect_true("mount for path exists", home_mount.has_value(), "missing mount for /home/user/file");
    test_helpers::expect_eq("mount for path target", home_mount->target, "/home");

    auto backup_mount = btrfsbackup::backup::mount_at(entries, "/mnt/backup");
    test_helpers::expect_true("mount at exists", backup_mount.has_value(), "missing mount at /mnt/backup");
    test_helpers::expect_eq("mount at source", backup_mount->source, "/dev/mapper/backupdisk[/snapshots]");

    test_helpers::expect_true(
        "same filesystem uuid",
        btrfsbackup::backup::paths_are_same_filesystem(entries, "/home/user", "/"),
        "source paths should share filesystem UUID"
    );
    test_helpers::expect_true(
        "different filesystem uuid",
        !btrfsbackup::backup::paths_are_same_filesystem(entries, "/home/user", "/mnt/backup"),
        "source and target should not share filesystem UUID"
    );
    test_helpers::expect_true(
        "mount uses mapper",
        btrfsbackup::backup::mount_uses_mapper(entries, "/mnt/backup", "/dev/mapper/backupdisk"),
        "backup mount should use expected mapper"
    );
    test_helpers::expect_true(
        "mount rejects mapper",
        !btrfsbackup::backup::mount_uses_mapper(entries, "/home", "/dev/mapper/backupdisk"),
        "home mount should not use backup mapper"
    );
}

void test_same_filesystem_device_fallback() {
    std::vector<btrfsbackup::backup::MountEntry> entries{
        {
            .source = "/dev/sda2",
            .target = "/",
            .fstype = "btrfs",
            .device_id = "0:20",
        },
        {
            .source = "/dev/sda2",
            .target = "/srv",
            .fstype = "btrfs",
            .device_id = "0:20",
        },
    };

    test_helpers::expect_true(
        "same filesystem fallback",
        btrfsbackup::backup::paths_are_same_filesystem(entries, "/srv/data", "/etc"),
        "paths should share device id when UUID is unavailable"
    );
}

void test_missing_mount_table_is_error() {
    test_helpers::expect_validation_error("missing mount table", [] { (void)btrfsbackup::platform::linux::read_mount_table("/tmp/does-not-exist-btrfs-backup-mountinfo"); }, "could not read mount table");
}

} // namespace

int main() {
    test_reads_mount_table();
    test_mount_lookup_and_matching();
    test_same_filesystem_device_fallback();
    test_missing_mount_table_is_error();

    return test_helpers::finish("mount info tests");
}
