// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <cli/installation_command.hpp>
#include <config/json.hpp>
#include <config/json_io.hpp>

#include "support/test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

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
    btrfsbackup::Json profile = {
        {"schemaVersion", 3},
        {"profileId", "laptop"},
        {"name", "Laptop backup"},
        {"enabled", true},
        {"target", {
            {"device", "/dev/disk/by-uuid/11111111-2222-3333-4444-555555555555"},
            {"luksUuid", "11111111-2222-3333-4444-555555555555"},
            {"btrfsUuid", "66666666-7777-8888-9999-aaaaaaaaaaaa"},
            {"mapperName", "backupdisk"}
        }},
        {"sources", btrfsbackup::Json::array({
            {
                {"id", "home"},
                {"name", "Home"},
                {"enabled", true},
                {"subvolume", "/home"},
                {"localSnapshotDir", "/.snapshots/btrfs-backup/home"},
                {"remoteSubdir", "home"},
                {"remoteRetention", 7},
                {"localRetention", 3}
            }
        })}
    };
    test_helpers::write_file(profile_json, btrfsbackup::dump_json(profile));

    int result = btrfsbackup::command::installation({
        "render",
        "--file",
        profile_json.string(),
        "--output-dir",
        (root / "rendered").string(),
        "--eject-script",
        "/usr/bin/btrfs-backupctl target eject",
        "--keyfile",
        "/root/keys/backupdisk.key",
    });

    test_helpers::expect_eq("installation render result", std::to_string(result), "0");
    test_helpers::expect_contains(
        "installation fstab",
        read_file(root / "rendered" / "config" / "fstab.fragment"),
        "/dev/mapper/backupdisk  /mnt/btrfs-backup/laptop  btrfs"
    );
    test_helpers::expect_contains(
        "installation fstab security options",
        read_file(root / "rendered" / "config" / "fstab.fragment"),
        "noatime,nodev,nosuid,noexec,nosymfollow,compress=zstd"
    );
    test_helpers::expect_contains(
        "installation crypttab",
        read_file(root / "rendered" / "config" / "crypttab.fragment"),
        "backupdisk  UUID=11111111-2222-3333-4444-555555555555  /root/keys/backupdisk.key"
    );
    test_helpers::expect_contains(
        "installation service",
        read_file(root / "rendered" / "systemd" / "btrfs-backup.service"),
        "ExecStart=/usr/bin/btrfs-backupctl runner execute --profile laptop"
    );
    test_helpers::expect_contains(
        "installation profile service",
        read_file(root / "rendered" / "systemd" / "btrfs-backup@.service"),
        "ExecStart=/usr/bin/btrfs-backupctl runner execute --profile %i"
    );
    test_helpers::expect_contains(
        "installation asynchronous eject",
        read_file(root / "rendered" / "systemd" / "btrfs-backup@.service"),
        "ExecStopPost=/usr/bin/systemctl --no-block start btrfs-backup-eject@%i.service"
    );
    test_helpers::expect_contains(
        "installation eject command",
        read_file(root / "rendered" / "systemd" / "btrfs-backup-eject@.service"),
        "ExecStart=/usr/bin/btrfs-backupctl target eject --from-service --profile %i"
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
    btrfsbackup::Json profile = {
        {"schemaVersion", 3},
        {"profileId", "laptop"},
        {"name", "Laptop backup"},
        {"enabled", true},
        {"target", {
            {"device", "/dev/disk/by-uuid/11111111-2222-3333-4444-555555555555"},
            {"luksUuid", "11111111-2222-3333-4444-555555555555"},
            {"btrfsUuid", "66666666-7777-8888-9999-aaaaaaaaaaaa"},
            {"mapperName", "backupdisk"}
        }},
        {"sources", btrfsbackup::Json::array({
            {
                {"id", "home"},
                {"name", "Home"},
                {"enabled", true},
                {"subvolume", "/home"},
                {"localSnapshotDir", "/.snapshots/btrfs-backup/home"},
                {"remoteSubdir", "home"}
            }
        })}
    };
    test_helpers::write_file(profile_json, btrfsbackup::dump_json(profile));

    int result = btrfsbackup::command::installation({
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
