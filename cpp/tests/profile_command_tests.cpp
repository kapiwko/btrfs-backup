#include <sys/stat.h>

#include <filesystem>
#include <string>

#include <btrfsbackup/json.hpp>
#include <btrfsbackup/json_io.hpp>
#include <btrfsbackup/command/profile_command.hpp>
#include <btrfsbackup/migrate_profile.hpp>
#include <btrfsbackup/source_definition.hpp>

#include "test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

fs::path test_root(const std::string& name) {
    return test_helpers::test_root("profile-command", name);
}

void test_parse_profile_sources_from_json() {
    fs::path root = test_root("profile-sources");
    btrfsbackup::Json profile = {
        {"schemaVersion", 1},
        {"profileId", "default"},
        {"name", "Default backup"},
        {"enabled", true},
        {"target", {
            {"device", (root / "dev/disk/by-uuid/11111111-2222-3333-4444-555555555555").string()},
            {"luksUuid", "11111111-2222-3333-4444-555555555555"},
            {"mapperName", "backupdisk"},
            {"mountPoint", "/mnt/backup"}
        }},
        {"sources", btrfsbackup::Json::array({
            {
                {"id", "root"},
                {"name", "Root"},
                {"enabled", true},
                {"subvolume", "/mnt/source/root"},
                {"localSnapshotDir", "/mnt/source/.snapshots/root"},
                {"remoteSubdir", "root"},
                {"remoteRetention", 7},
                {"localRetention", 3}
            },
            {
                {"id", "home"},
                {"name", "Home"},
                {"enabled", false},
                {"subvolume", "/mnt/source/home"},
                {"localSnapshotDir", "/mnt/source/.snapshots/home"},
                {"remoteSubdir", "home"},
                {"remoteRetention", 7},
                {"localRetention", 3}
            }
        })}
    };
    fs::path profile_json = root / "profile.json";
    test_helpers::write_file(profile_json, btrfsbackup::dump_json(profile));

    std::ostringstream output;
    btrfsbackup::command_parse_profile_sources(
        {
            "--file",
            profile_json.string(),
        },
        output
    );

    test_helpers::expect_eq(
        "profile sources",
        output.str(),
        "root\n/mnt/source/root\n/mnt/source/.snapshots/root\nroot\n7\n3\n"
    );
    fs::remove_all(root);
}

void test_profile_create_command_writes_json() {
    fs::path root = test_root("profile-create");
    fs::path profile_json = root / "profile.json";

    int result = btrfsbackup::command::profile({
        "create",
        "--output",
        profile_json.string(),
        "--profile",
        "default",
        "--name",
        "Default backup",
        "--device",
        "/dev/disk/by-uuid/11111111-2222-3333-4444-555555555555",
        "--luks-uuid",
        "11111111-2222-3333-4444-555555555555",
        "--btrfs-uuid",
        "66666666-7777-8888-9999-aaaaaaaaaaaa",
        "--mapper-name",
        "backupdisk",
        "--mount-point",
        "/mnt/backup",
        "--remote-retention",
        "2",
        "--local-retention",
        "2",
        "--source",
        "home",
        "Home",
        "/home",
        "/.snapshots/btrfs-backup/home",
        "home",
        "2",
        "2",
    });

    test_helpers::expect_eq("profile create result", std::to_string(result), "0");
    btrfsbackup::Json profile = btrfsbackup::load_json_file(profile_json);
    test_helpers::expect_eq("profile create id", profile.at("profileId").get<std::string>(), "default");
    test_helpers::expect_eq("profile create source id", profile.at("sources").at(0).at("id").get<std::string>(), "home");
    test_helpers::expect_eq("profile create remote root", profile.at("paths").at("remoteRoot").get<std::string>(), "/mnt/backup/snapshots");
    fs::remove_all(root);
}

void test_migrate_profile_creates_profile_files() {
    fs::path root = test_root("migrate-profile");
    fs::path source_config = root / "legacy.env";
    fs::path source_dir = root / "sources.d";
    fs::path profile_dir = root / "profiles.d";
    fs::path udev_dir = root / "udev";
    fs::path public_dir = root / "public";

    test_helpers::write_file(
        source_config,
        "BACKUP_MAPPER_NAME=backupdisk\n"
        "BACKUP_DEVICE=/dev/disk/by-uuid/11111111-2222-3333-4444-555555555555\n"
        "BACKUP_LUKS_UUID=11111111-2222-3333-4444-555555555555\n"
        "BACKUP_BTRFS_UUID=66666666-7777-8888-9999-aaaaaaaaaaaa\n"
        "BACKUP_MOUNTPOINT=/mnt/backup\n"
        "SOURCES_DIR=" + source_dir.string() + "\n"
        "RETENTION_COUNT=30\n"
        "LOCAL_RETENTION_COUNT=20\n"
    );
    test_helpers::write_file(
        source_dir / "10-home.conf",
        "SOURCE_NAME=home\n"
        "SOURCE_DISPLAY_NAME=Home\n"
        "SOURCE_SUBVOLUME=/home\n"
        "LOCAL_SNAPSHOT_DIR=/.snapshots/btrfs-backup/home\n"
        "REMOTE_SUBDIR=home\n"
        "SOURCE_RETENTION_COUNT=45\n"
        "SOURCE_LOCAL_RETENTION_COUNT=20\n"
    );
    chmod(source_config.c_str(), 0600);
    chmod((source_dir / "10-home.conf").c_str(), 0600);

    int result = btrfsbackup::command_migrate_profile({
        "--source", source_config.string(),
        "--profile-dir", profile_dir.string(),
        "--udev-dir", udev_dir.string(),
        "--public-dir", public_dir.string(),
        "--profile", "default",
        "--name", "Default backup",
    });

    test_helpers::expect_eq("migrate result", std::to_string(result), "0");
    test_helpers::expect_true("migrate env", !fs::exists(profile_dir / "default.env"), "profile env should not be generated");
    test_helpers::expect_true("migrate json", fs::is_regular_file(root / "profiles" / "default" / "profile.json"), "missing profile JSON");
    test_helpers::expect_true("migrate udev", fs::is_regular_file(udev_dir / "99-btrfs-backup-default.rules"), "missing udev rule");
    test_helpers::expect_true("migrate public", fs::is_regular_file(public_dir / "default.json"), "missing public profile");

    btrfsbackup::Json profile = btrfsbackup::load_json_file(root / "profiles" / "default" / "profile.json");
    test_helpers::expect_eq("migrate profile id", profile.at("profileId").get<std::string>(), "default");
    test_helpers::expect_eq("migrate source id", profile.at("sources").at(0).at("id").get<std::string>(), "home");
    fs::remove_all(root);
}

} // namespace

int main() {
    test_parse_profile_sources_from_json();
    test_profile_create_command_writes_json();
    test_migrate_profile_creates_profile_files();

    return test_helpers::finish("profile command tests");
}
