#include <sys/stat.h>

#include <filesystem>
#include <string>

#include <btrfsbackup/json.hpp>
#include <btrfsbackup/json_io.hpp>
#include <btrfsbackup/command/profile_command.hpp>
#include <btrfsbackup/command/profile_sources_command.hpp>

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
            {"btrfsUuid", "66666666-7777-8888-9999-aaaaaaaaaaaa"},
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
    btrfsbackup::command::profile_sources(
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

void test_profile_create_writes_json() {
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

} // namespace

int main() {
    test_parse_profile_sources_from_json();
    test_profile_create_writes_json();

    return test_helpers::finish("profile command tests");
}
