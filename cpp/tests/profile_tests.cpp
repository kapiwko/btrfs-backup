#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>

#include <btrfsbackup/errors.hpp>
#include <btrfsbackup/file_io.hpp>
#include <btrfsbackup/json.hpp>
#include <btrfsbackup/json_io.hpp>
#include <btrfsbackup/profile.hpp>
#include <btrfsbackup/profile_render.hpp>
#include <btrfsbackup/profile_store.hpp>

namespace fs = std::filesystem;

namespace {

using btrfsbackup::Json;
using btrfsbackup::ValidationError;

int failures = 0;

void fail(const std::string& name, const std::string& message) {
    ++failures;
    std::cerr << "not ok - " << name << ": " << message << '\n';
}

void expect_true(const std::string& name, bool condition, const std::string& message) {
    if (!condition) {
        fail(name, message);
    }
}

void expect_validation_error(const std::string& name, const std::function<void()>& fn, const std::string& expected) {
    try {
        fn();
        fail(name, "expected ValidationError");
    } catch (const ValidationError& exc) {
        std::string message = exc.what();
        if (message.find(expected) == std::string::npos) {
            fail(name, "unexpected error: " + message);
        }
    } catch (const std::exception& exc) {
        fail(name, std::string("unexpected exception: ") + exc.what());
    }
}

Json valid_profile() {
    return {
        {"schemaVersion", 1},
        {"profileId", "default"},
        {"name", "Default backup"},
        {"enabled", true},
        {"target", {
            {"device", "/dev/disk/by-uuid/11111111-2222-3333-4444-555555555555"},
            {"luksUuid", "11111111-2222-3333-4444-555555555555"},
            {"btrfsUuid", "66666666-7777-8888-9999-aaaaaaaaaaaa"},
            {"partitionUuid", "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"},
            {"serial", "SERIAL_123"},
            {"mapperName", "backupdisk"},
            {"mountPoint", "/mnt/backup"}
        }},
        {"paths", {
            {"sourcesDir", "/etc/btrfs-backup/profiles/default/sources.d"},
            {"remoteRoot", "/mnt/backup/snapshots"},
            {"incomingRoot", "/mnt/backup/.incoming"},
            {"stateDir", "/var/lib/btrfs-backup"},
            {"statusRoot", "/run/btrfs-backup/profiles"},
            {"historyRoot", "/var/lib/btrfs-backup/history"}
        }},
        {"settings", {
            {"dailyLimit", true},
            {"incrementalRequired", true},
            {"keepFailedLocalSnapshot", false},
            {"autoEject", true},
            {"remoteRetention", 30},
            {"localRetention", 20},
            {"minimumTargetFreeBytes", 5368709120LL},
            {"minimumLocalFreeBytes", 1073741824LL}
        }},
        {"notifications", {
            {"enabled", true},
            {"user", "tester"},
            {"method", "auto"}
        }},
        {"sources", Json::array({
            {
                {"id", "home"},
                {"name", "Home"},
                {"enabled", true},
                {"subvolume", "/home"},
                {"localSnapshotDir", "/.snapshots/btrfs-backup/home"},
                {"remoteSubdir", "home"},
                {"remoteRetention", 30},
                {"localRetention", 20}
            }
        })}
    };
}

fs::path test_root() {
    fs::path root = fs::temp_directory_path() / ("btrfsbackup-cpp-tests-" + std::to_string(getpid()));
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}

void test_rejects_bad_uuid() {
    Json profile = valid_profile();
    profile["target"]["luksUuid"] = "bad";
    expect_validation_error("bad uuid", [&] { btrfsbackup::normalize_profile(profile); }, "target.luksUuid");
}

void test_rejects_non_dev_target() {
    Json profile = valid_profile();
    profile["target"]["device"] = "/tmp/not-a-device";
    expect_validation_error("non-dev target", [&] { btrfsbackup::normalize_profile(profile); }, "target.device");
}

void test_rejects_nested_roots() {
    Json profile = valid_profile();
    profile["paths"]["incomingRoot"] = "/mnt/backup/snapshots/.incoming";
    expect_validation_error("nested roots", [&] { btrfsbackup::normalize_profile(profile); }, "remoteRoot and paths.incomingRoot");
}

void test_profile_round_trips_normalized_json() {
    Json normalized = btrfsbackup::normalize_profile(valid_profile());
    btrfsbackup::Profile profile = btrfsbackup::profile_from_json(normalized);
    Json round_trip = btrfsbackup::profile_to_json(profile);

    expect_true("profile model id", profile.id == "default", "wrong profile id");
    expect_true("profile model source", profile.sources.size() == 1 && profile.sources.at(0).id == "home", "wrong profile source");
    expect_true("profile model round trip", round_trip == normalized, "typed profile did not preserve normalized JSON");
}

void test_render_profile_env_quotes_values() {
    btrfsbackup::Profile profile = btrfsbackup::profile_from_json(valid_profile());
    std::string rendered = btrfsbackup::render_profile_env(profile);
    expect_true("profile env quote", rendered.find("PROFILE_NAME='Default backup'\n") != std::string::npos, "profile name was not shell quoted");
    expect_true(
        "profile env eject script",
        rendered.find("EJECT_SCRIPT_PATH=/usr/lib/btrfs-backup/btrfs-backup-eject.sh\n") != std::string::npos,
        "eject script path was not rendered as a string"
    );
}

void test_typed_store_renders_tree() {
    fs::path root = test_root();
    btrfsbackup::Profile profile = btrfsbackup::profile_from_json(valid_profile());

    btrfsbackup::render_tree(profile, root / "rendered");

    expect_true("typed tree profile env", !fs::exists(root / "rendered" / "etc" / "btrfs-backup" / "profiles.d" / "default.env"), "profile env should not be rendered");
    expect_true(
        "typed tree profile json",
        fs::is_regular_file(root / "rendered" / "etc" / "btrfs-backup" / "profiles" / "default" / "profile.json"),
        "missing rendered profile JSON"
    );
    expect_true(
        "typed tree public",
        fs::is_regular_file(root / "rendered" / "var" / "lib" / "btrfs-backup" / "public" / "profiles" / "default.json"),
        "missing rendered public profile"
    );
    fs::remove_all(root);
}

void test_typed_store_saves_tree() {
    fs::path root = test_root();
    btrfsbackup::Profile profile = btrfsbackup::profile_from_json(valid_profile());
    profile.paths.sources_dir = "/etc/btrfs-backup/profiles/default/sources.d";

    btrfsbackup::save_tree(
        profile,
        root / "etc" / "btrfs-backup",
        root / "etc" / "udev" / "rules.d",
        root / "public"
    );

    expect_true("typed save profile env", !fs::exists(root / "etc" / "btrfs-backup" / "profiles.d" / "default.env"), "profile env should not be saved");
    expect_true(
        "typed save profile json",
        fs::is_regular_file(root / "etc" / "btrfs-backup" / "profiles" / "default" / "profile.json"),
        "missing saved profile JSON"
    );
    expect_true(
        "typed save public",
        fs::is_regular_file(root / "public" / "default.json"),
        "missing saved public profile"
    );
    fs::remove_all(root);
}

void test_render_udev_optional_matches() {
    btrfsbackup::Profile profile = btrfsbackup::profile_from_json(valid_profile());
    std::string rendered = btrfsbackup::render_udev(profile);
    expect_true("udev partition", rendered.find("ENV{ID_PART_ENTRY_UUID}==\"aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee\"") != std::string::npos, "missing partition UUID match");
    expect_true("udev serial", rendered.find("ENV{ID_SERIAL_SHORT}==\"SERIAL_123\"") != std::string::npos, "missing serial match");
}

} // namespace

int main() {
    test_rejects_bad_uuid();
    test_rejects_non_dev_target();
    test_rejects_nested_roots();
    test_profile_round_trips_normalized_json();
    test_render_profile_env_quotes_values();
    test_typed_store_renders_tree();
    test_typed_store_saves_tree();
    test_render_udev_optional_matches();

    if (failures > 0) {
        return 1;
    }
    std::cout << "ok - profile module tests\n";
    return 0;
}
