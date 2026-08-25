// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <sys/stat.h>

#include <filesystem>
#include <string>

#include <config/application_config.hpp>

#include "support/test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

void write_config(const fs::path& root, const std::string& content, mode_t mode = 0600) {
    fs::path path = root / "btrfs-backup.conf";
    test_helpers::write_file(path, content);
    chmod(path.c_str(), mode);
}

void test_defaults_when_config_is_absent() {
    fs::path root = test_helpers::test_root("application-config", "defaults");
    btrfsbackup::ApplicationConfig config = btrfsbackup::ApplicationConfig::load(root);

    test_helpers::expect_eq("default sources root", config.paths().sources_root.string(), (root / "profiles").string());
    test_helpers::expect_eq("default state root", config.paths().state_root.string(), "/var/lib/btrfs-backup");
    test_helpers::expect_eq("default status root", config.paths().status_root.string(), "/run/btrfs-backup/profiles");
    test_helpers::expect_eq("default history root", config.paths().history_root.string(), "/var/lib/btrfs-backup/history");
    test_helpers::expect_eq("default target mount root", config.paths().target_mount_root.string(), "/mnt/btrfs-backup");
    fs::remove_all(root);
}

void test_loads_trusted_global_paths() {
    fs::path root = test_helpers::test_root("application-config", "custom");
    write_config(
        root,
        "# trusted administrator configuration\n"
        "CONFIG_VERSION=1\n"
        "SOURCES_ROOT=/srv/btrfs-backup/profiles\n"
        "STATE_ROOT=/srv/btrfs-backup/state\n"
        "STATUS_ROOT=/run/custom-btrfs-backup\n"
        "HISTORY_ROOT=/srv/btrfs-backup/history\n"
        "TARGET_MOUNT_ROOT=/srv/btrfs-backup/mounts\n",
        0644
    );

    btrfsbackup::ApplicationConfig config = btrfsbackup::ApplicationConfig::load(root);
    test_helpers::expect_eq("custom sources root", config.paths().sources_root.string(), "/srv/btrfs-backup/profiles");
    test_helpers::expect_eq("custom state root", config.paths().state_root.string(), "/srv/btrfs-backup/state");
    test_helpers::expect_eq("custom status root", config.paths().status_root.string(), "/run/custom-btrfs-backup");
    test_helpers::expect_eq("custom history root", config.paths().history_root.string(), "/srv/btrfs-backup/history");
    test_helpers::expect_eq("custom target mount root", config.paths().target_mount_root.string(), "/srv/btrfs-backup/mounts");
    fs::remove_all(root);
}

void test_rejects_untrusted_or_unknown_configuration() {
    fs::path root = test_helpers::test_root("application-config", "permissions");
    write_config(root, "CONFIG_VERSION=1\n", 0664);
    test_helpers::expect_validation_error("writable application config", [&] {
        (void)btrfsbackup::ApplicationConfig::load(root);
    }, "must not be writable by group or others");

    write_config(root, "CONFIG_VERSION=1\nPROFILE_STATUS_ROOT=/etc\n");
    test_helpers::expect_validation_error("unknown application key", [&] {
        (void)btrfsbackup::ApplicationConfig::load(root);
    }, "PROFILE_STATUS_ROOT is not supported");
    fs::remove_all(root);
}

void test_rejects_config_symlink() {
    fs::path root = test_helpers::test_root("application-config", "symlink");
    fs::path target = root / "real.conf";
    test_helpers::write_file(target, "CONFIG_VERSION=1\n");
    chmod(target.c_str(), 0644);
    fs::create_symlink(target, root / "btrfs-backup.conf");

    test_helpers::expect_validation_error("application config symlink", [&] {
        (void)btrfsbackup::ApplicationConfig::load(root);
    }, "not a regular file");
    fs::remove_all(root);
}

} // namespace

int main() {
    test_defaults_when_config_is_absent();
    test_loads_trusted_global_paths();
    test_rejects_untrusted_or_unknown_configuration();
    test_rejects_config_symlink();
    return test_helpers::finish("application config tests");
}
