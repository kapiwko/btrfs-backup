// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <cli/InstallationCommand.hpp>
#include <config/json/Json.hpp>
#include <config/json/JsonIo.hpp>

#include "support/TestHelpers.hpp"

namespace fs = std::filesystem;

namespace {

#ifndef BTRFSBACKUP_TEST_INSTALL_BINDIR
#error "BTRFSBACKUP_TEST_INSTALL_BINDIR must be defined by the build system"
#endif

fs::path test_root(const std::string& name) {
    return test_helpers::test_root("installation", name);
}

std::string read_file(const fs::path& path) {
    std::ifstream stream(path);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void expect_service_hardening(const std::string& name, const std::string& unit) {
    const std::vector<std::string> directives = {
        "NoNewPrivileges=yes",
        "PrivateTmp=yes",
        "ProtectSystem=full",
        "ProtectKernelTunables=yes",
        "ProtectKernelModules=yes",
        "ProtectControlGroups=yes",
        "ProtectHostname=yes",
        "ProtectClock=yes",
        "ProtectProc=invisible",
        "LockPersonality=yes",
        "RestrictRealtime=yes",
        "MemoryDenyWriteExecute=yes",
        "SystemCallArchitectures=native",
        "RestrictAddressFamilies=AF_UNIX AF_NETLINK",
        "Environment=PATH=/usr/bin",
    };
    for (const std::string& directive : directives) {
        test_helpers::expect_contains(name + " " + directive, unit, directive + "\n");
    }
}

void test_installation_render_writes_static_files() {
    fs::path root = test_root("render");
    fs::path profile_json = root / "profile.json";
    btrfsbackup::config::json::Json profile = {
        {"schemaVersion", 4},
        {"profileId", "laptop"},
        {"name", "Laptop backup"},
        {"enabled", true},
        {"target", {{"device", "/dev/disk/by-uuid/11111111-2222-3333-4444-555555555555"}, {"luksUuid", "11111111-2222-3333-4444-555555555555"}, {"btrfsUuid", "66666666-7777-8888-9999-aaaaaaaaaaaa"}, {"mapperName", "backupdisk"}, {"activation", {{"mode", "askPassword"}}}}},
        {"sources", btrfsbackup::config::json::Json::array({{{"id", "home"}, {"name", "Home"}, {"enabled", true}, {"subvolume", "/home"}, {"localSnapshotDir", "/.snapshots/btrfs-backup/home"}, {"remoteSubdir", "home"}, {"remoteRetention", 7}, {"localRetention", 3}}})}
    };
    test_helpers::write_file(profile_json, btrfsbackup::config::json::dump_json(profile));

    int result = btrfsbackup::cli::installation({
        "render",
        "--file",
        profile_json.string(),
        "--output-dir",
        (root / "rendered").string(),
        "--eject-script",
        "/usr/bin/btrfs-backupctl target eject",
    });
    const std::string backup_command =
        std::string(BTRFSBACKUP_TEST_INSTALL_BINDIR) + "/btrfs-backupctl runner execute";

    test_helpers::expect_eq("installation render result", std::to_string(result), "0");
    test_helpers::expect_true(
        "installation has no table fragments",
        !fs::exists(root / "rendered" / "config" / "fstab.fragment") &&
            !fs::exists(root / "rendered" / "config" / "crypttab.fragment"),
        "installation render still generated fstab or crypttab fragments"
    );
    test_helpers::expect_contains(
        "installation native mount",
        read_file(root / "rendered" / "systemd" / "mnt-btrfs\\x2dbackup-laptop.mount"),
        "Requires=btrfs-backup-target@laptop.service"
    );
    test_helpers::expect_contains(
        "installation native mount security options",
        read_file(root / "rendered" / "systemd" / "mnt-btrfs\\x2dbackup-laptop.mount"),
        "Options=noatime,nodev,nosuid,noexec,nosymfollow,compress=zstd"
    );
    test_helpers::expect_contains(
        "installation target activation command",
        read_file(root / "rendered" / "systemd" / "btrfs-backup-target@.service"),
        "btrfs-backupctl target activate --from-service --profile %i"
    );
    test_helpers::expect_contains(
        "installation service",
        read_file(root / "rendered" / "systemd" / "btrfs-backup.service"),
        "ExecStart=" + backup_command + " --profile laptop"
    );
    test_helpers::expect_contains(
        "installation profile service",
        read_file(root / "rendered" / "systemd" / "btrfs-backup@.service"),
        "ExecStart=" + backup_command + " --profile %i"
    );
    test_helpers::expect_contains(
        "installation successful eject",
        read_file(root / "rendered" / "systemd" / "btrfs-backup@.service"),
        "OnSuccess=btrfs-backup-eject@%i.service"
    );
    test_helpers::expect_contains(
        "installation failed eject",
        read_file(root / "rendered" / "systemd" / "btrfs-backup@.service"),
        "OnFailure=btrfs-backup-eject@%i.service"
    );
    test_helpers::expect_contains(
        "installation eject command",
        read_file(root / "rendered" / "systemd" / "btrfs-backup-eject@.service"),
        "ExecStart=/usr/bin/btrfs-backupctl target eject --from-service --profile %i"
    );
    test_helpers::expect_contains(
        "installation validation command",
        read_file(root / "rendered" / "systemd" / "btrfs-backup-validate@.service"),
        "ExecStart=" + backup_command + " --profile ${BTRFS_BACKUP_PROFILE_ID} --validate"
    );
    test_helpers::expect_contains(
        "installation validation context",
        read_file(root / "rendered" / "systemd" / "btrfs-backup-validate@.service"),
        "EnvironmentFile=/run/btrfs-backup-manager/%i.env"
    );
    test_helpers::expect_contains(
        "installation stop timeout",
        read_file(root / "rendered" / "systemd" / "btrfs-backup@.service"),
        "TimeoutStopSec=90s"
    );
    test_helpers::expect_contains(
        "installation kill mode",
        read_file(root / "rendered" / "systemd" / "btrfs-backup@.service"),
        "KillMode=mixed"
    );
    expect_service_hardening(
        "installation service hardening",
        read_file(root / "rendered" / "systemd" / "btrfs-backup.service")
    );
    expect_service_hardening(
        "installation profile service hardening",
        read_file(root / "rendered" / "systemd" / "btrfs-backup@.service")
    );
    expect_service_hardening(
        "installation validation service hardening",
        read_file(root / "rendered" / "systemd" / "btrfs-backup-validate@.service")
    );
    test_helpers::expect_contains(
        "installation profile mount dependency",
        read_file(root / "rendered" / "systemd" / "btrfs-backup@laptop.service.d" / "target-mount.conf"),
        "RequiresMountsFor=\"/mnt/btrfs-backup/laptop\""
    );
    test_helpers::expect_contains(
        "installation static service mount dependency",
        read_file(root / "rendered" / "systemd" / "btrfs-backup.service"),
        "RequiresMountsFor=\"/mnt/btrfs-backup/laptop\""
    );
    fs::remove_all(root);
}

void test_installation_render_allows_explicit_backup_command_override() {
    fs::path root = test_root("render-backup-command");
    fs::path profile_json = root / "profile.json";
    btrfsbackup::config::json::Json profile = {
        {"schemaVersion", 4},
        {"profileId", "laptop"},
        {"name", "Laptop backup"},
        {"enabled", true},
        {"target", {{"device", "/dev/disk/by-uuid/11111111-2222-3333-4444-555555555555"}, {"luksUuid", "11111111-2222-3333-4444-555555555555"}, {"btrfsUuid", "66666666-7777-8888-9999-aaaaaaaaaaaa"}, {"mapperName", "backupdisk"}, {"activation", {{"mode", "askPassword"}}}}},
        {"sources", btrfsbackup::config::json::Json::array({{{"id", "home"}, {"name", "Home"}, {"enabled", true}, {"subvolume", "/home"}, {"localSnapshotDir", "/.snapshots/btrfs-backup/home"}, {"remoteSubdir", "home"}}})}
    };
    test_helpers::write_file(profile_json, btrfsbackup::config::json::dump_json(profile));

    int result = btrfsbackup::cli::installation({
        "render",
        "--file",
        profile_json.string(),
        "--output-dir",
        (root / "rendered").string(),
        "--backup-command",
        "/usr/bin/btrfs-backup",
    });

    test_helpers::expect_eq("installation render override result", std::to_string(result), "0");
    test_helpers::expect_contains(
        "installation override service",
        read_file(root / "rendered" / "systemd" / "btrfs-backup@.service"),
        "ExecStart=/usr/bin/btrfs-backup --profile %i"
    );
    fs::remove_all(root);
}

} // namespace

int main() {
    test_installation_render_writes_static_files();
    test_installation_render_allows_explicit_backup_command_override();

    return test_helpers::finish("installation tests");
}
