#include <filesystem>
#include <fstream>
#include <string>

#include <btrfsbackup/command/installation_command.hpp>
#include <btrfsbackup/json.hpp>
#include <btrfsbackup/json_io.hpp>

#include "test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

fs::path test_root(const std::string& name) {
    return test_helpers::test_root("installation", name);
}

std::string read_file(const fs::path& path) {
    std::ifstream stream(path);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void test_installation_render_writes_static_files() {
    fs::path root = test_root("render");
    fs::path profile_json = root / "profile.json";
    btrfsbackup::Json profile = {
        {"schemaVersion", 1},
        {"profileId", "laptop"},
        {"name", "Laptop backup"},
        {"enabled", true},
        {"target", {
            {"device", "/dev/disk/by-uuid/11111111-2222-3333-4444-555555555555"},
            {"luksUuid", "11111111-2222-3333-4444-555555555555"},
            {"btrfsUuid", "66666666-7777-8888-9999-aaaaaaaaaaaa"},
            {"mapperName", "backupdisk"},
            {"mountPoint", "/mnt/backup"}
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
        "/dev/mapper/backupdisk  /mnt/backup  btrfs"
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
    fs::remove_all(root);
}

void test_installation_render_allows_explicit_backup_command_override() {
    fs::path root = test_root("render-backup-command");
    fs::path profile_json = root / "profile.json";
    btrfsbackup::Json profile = {
        {"schemaVersion", 1},
        {"profileId", "laptop"},
        {"name", "Laptop backup"},
        {"enabled", true},
        {"target", {
            {"device", "/dev/disk/by-uuid/11111111-2222-3333-4444-555555555555"},
            {"luksUuid", "11111111-2222-3333-4444-555555555555"},
            {"btrfsUuid", "66666666-7777-8888-9999-aaaaaaaaaaaa"},
            {"mapperName", "backupdisk"},
            {"mountPoint", "/mnt/backup"}
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
