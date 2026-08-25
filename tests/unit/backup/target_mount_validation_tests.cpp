// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <string>
#include <vector>

#include <backup/target_mount_validation.hpp>

#include "support/test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

btrfsbackup::Profile profile(const fs::path& mount_point = "/mnt/backup") {
    btrfsbackup::Profile result;
    result.id = "default";
    result.target.mapper_name = "backup";
    result.target.mount_point = mount_point.string();
    result.target.btrfs_uuid = "target-fs";
    result.paths.remote_root = (mount_point / "snapshots").string();
    result.paths.incoming_root = (mount_point / ".incoming").string();
    return result;
}

std::vector<btrfsbackup::MountEntry> mounts(const fs::path& mount_point = "/mnt/backup") {
    return {
        {
            .source = "/dev/mapper/backup",
            .target = mount_point.string(),
            .fstype = "btrfs",
            .options = "rw,relatime,nodev,nosuid,noexec,nosymfollow",
            .device_id = "0:21",
            .filesystem_uuid = "target-fs",
        },
    };
}

void test_accepts_valid_target_mount() {
    btrfsbackup::validate_target_mount(profile(), mounts());
}

void test_rejects_missing_mount() {
    test_helpers::expect_validation_error("missing target mount", [] {
        btrfsbackup::validate_target_mount(profile(), {});
    }, "not mounted");
}

void test_rejects_wrong_filesystem_type() {
    std::vector<btrfsbackup::MountEntry> entries = mounts();
    entries.at(0).fstype = "ext4";

    test_helpers::expect_validation_error("wrong filesystem", [&] {
        btrfsbackup::validate_target_mount(profile(), entries);
    }, "not a Btrfs filesystem");
}

void test_rejects_wrong_mapper() {
    std::vector<btrfsbackup::MountEntry> entries = mounts();
    entries.at(0).source = "/dev/mapper/other";

    test_helpers::expect_validation_error("wrong mapper", [&] {
        btrfsbackup::validate_target_mount(profile(), entries);
    }, "is not /dev/mapper/backup");
}

void test_rejects_read_only_mount() {
    std::vector<btrfsbackup::MountEntry> entries = mounts();
    entries.at(0).options = "ro,relatime";

    test_helpers::expect_validation_error("read-only target", [&] {
        btrfsbackup::validate_target_mount(profile(), entries);
    }, "not mounted read-write");
}

void test_rejects_missing_security_mount_options() {
    for (const std::string missing : {"nodev", "nosuid", "noexec", "nosymfollow"}) {
        std::vector<btrfsbackup::MountEntry> entries = mounts();
        entries.at(0).options = "rw,relatime";
        for (const std::string option : {"nodev", "nosuid", "noexec", "nosymfollow"}) {
            if (option != missing) {
                entries.at(0).options += "," + option;
            }
        }

        test_helpers::expect_validation_error("missing " + missing, [&] {
            btrfsbackup::validate_target_mount(profile(), entries);
        }, "missing required mount option " + missing);
    }
}

void test_rejects_uuid_mismatch() {
    std::vector<btrfsbackup::MountEntry> entries = mounts();
    entries.at(0).filesystem_uuid = "other-fs";

    test_helpers::expect_validation_error("uuid mismatch", [&] {
        btrfsbackup::validate_target_mount(profile(), entries);
    }, "Btrfs UUID mismatch");
}

void test_rejects_empty_configured_uuid() {
    btrfsbackup::Profile test_profile = profile();
    test_profile.target.btrfs_uuid = "";
    std::vector<btrfsbackup::MountEntry> entries = mounts();

    test_helpers::expect_validation_error("empty configured uuid", [&] {
        btrfsbackup::validate_target_mount(test_profile, entries);
    }, "target.btrfsUuid is required");
}

void test_rejects_remote_root_symlink_escape() {
    fs::path root = test_helpers::test_root("target-mount-validation", "remote-escape");
    fs::path mount_point = root / "mnt" / "backup";
    fs::path outside = root / "outside";
    fs::create_directories(mount_point);
    fs::create_directories(outside);
    fs::create_directory_symlink(outside, mount_point / "escape");

    btrfsbackup::Profile test_profile = profile(mount_point);
    test_profile.paths.remote_root = (mount_point / "escape" / "snapshots").string();

    test_helpers::expect_validation_error("remote root escape", [&] {
        btrfsbackup::validate_target_mount(test_profile, mounts(mount_point));
    }, "REMOTE_ROOT escapes");

    fs::remove_all(root);
}

void test_rejects_incoming_root_symlink_escape() {
    fs::path root = test_helpers::test_root("target-mount-validation", "incoming-escape");
    fs::path mount_point = root / "mnt" / "backup";
    fs::path outside = root / "outside";
    fs::create_directories(mount_point);
    fs::create_directories(outside);
    fs::create_directory_symlink(outside, mount_point / "escape");

    btrfsbackup::Profile test_profile = profile(mount_point);
    test_profile.paths.incoming_root = (mount_point / "escape" / ".incoming").string();

    test_helpers::expect_validation_error("incoming root escape", [&] {
        btrfsbackup::validate_target_mount(test_profile, mounts(mount_point));
    }, "INCOMING_ROOT escapes");

    fs::remove_all(root);
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
    test_rejects_empty_configured_uuid();
    test_rejects_remote_root_symlink_escape();
    test_rejects_incoming_root_symlink_escape();

    return test_helpers::finish("target mount validation tests");
}
