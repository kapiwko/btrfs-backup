#include <filesystem>
#include <string>

#include <config/installation_service.hpp>
#include <config/profile_service.hpp>
#include <state/status_service.hpp>
#include <config/json_io.hpp>

#include "support/test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

fs::path test_root(const std::string& name) {
    return test_helpers::test_root("application-services", name);
}

btrfsbackup::Profile sample_profile() {
    return btrfsbackup::profile_from_json({
        {"schemaVersion", 2},
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
        {"sources", btrfsbackup::Json::array({{
            {"id", "home"},
            {"name", "Home"},
            {"enabled", true},
            {"subvolume", "/home"},
            {"localSnapshotDir", "/.snapshots/btrfs-backup/home"},
            {"remoteSubdir", "home"},
            {"remoteRetention", 7},
            {"localRetention", 3}
        }})}
    });
}

void test_profile_and_installation_use_cases() {
    fs::path root = test_root("profile-installation");
    fs::path profile_file = root / "profiles" / "laptop" / "profile.json";
    btrfsbackup::write_profile_file(sample_profile(), profile_file);

    btrfsbackup::Profile loaded = btrfsbackup::validate_profile_file(profile_file);
    test_helpers::expect_eq("validated profile", loaded.id, "laptop");
    auto profiles = btrfsbackup::list_profiles(root / "profiles");
    test_helpers::expect_eq("listed profile", profiles.at(0), "laptop");

    btrfsbackup::render_installation({profile_file, root / "rendered", {}});
    test_helpers::expect_true(
        "rendered installation",
        fs::is_regular_file(root / "rendered" / "config" / "fstab.fragment"),
        "fstab fragment was not rendered"
    );
    fs::remove_all(root);
}

void test_status_use_cases() {
    fs::path root = test_root("status");
    const std::string status =
        "{\"schemaVersion\":3,\"state\":\"running\",\"errorCode\":\"\","
        "\"sourceName\":\"Home\",\"targetName\":\"Backup\",\"speedBps\":1,"
        "\"etaSeconds\":2,\"sourceProgress\":3,\"overallProgress\":4,"
        "\"progressAccuracy\":\"estimated\"}\n";
    test_helpers::write_file(root / "status" / "laptop" / "current.json", status);
    test_helpers::write_file(root / "history" / "laptop" / "2026-08-24T000000Z.json", "{\"state\":\"ok\"}");

    auto current = btrfsbackup::poll_status(root / "status", "laptop", "");
    test_helpers::expect_true("polled status", current.has_value(), "current status was not returned");
    test_helpers::expect_true("parsed status", current->data.at("state") == "running", "wrong status state");
    auto history = btrfsbackup::get_status_history(root / "history", "laptop", 10);
    test_helpers::expect_eq("status history size", std::to_string(history.size()), "1");
    fs::remove_all(root);
}

} // namespace

int main() {
    test_profile_and_installation_use_cases();
    test_status_use_cases();
    return test_helpers::finish("application services tests");
}
