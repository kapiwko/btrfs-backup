#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>

#include <config/errors.hpp>
#include <platform/linux/file_io.hpp>
#include <config/json.hpp>
#include <config/json_io.hpp>
#include <config/profile.hpp>
#include <config/profile_render.hpp>
#include <config/profile_store.hpp>

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
        {"schemaVersion", 2},
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
            {"remoteRoot", "/mnt/backup/snapshots"},
            {"incomingRoot", "/mnt/backup/.incoming"}
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

void test_rejects_missing_btrfs_uuid() {
    Json profile = valid_profile();
    profile["target"].erase("btrfsUuid");
    expect_validation_error("missing btrfs uuid", [&] { btrfsbackup::normalize_profile(profile); }, "btrfsUuid");
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

void test_profile_migrates_safe_legacy_system_paths() {
    Json legacy = valid_profile();
    legacy["schemaVersion"] = 1;
    legacy["paths"]["sourcesDir"] = "/etc/btrfs-backup/profiles/default/sources.d";
    legacy["paths"]["stateDir"] = "/var/lib/btrfs-backup";
    legacy["paths"]["statusRoot"] = "/run/btrfs-backup/profiles";
    legacy["paths"]["historyRoot"] = "/var/lib/btrfs-backup/history";

    Json normalized = btrfsbackup::normalize_profile(legacy);
    expect_true("legacy migrated schema", normalized.at("schemaVersion") == 2, "legacy profile was not migrated");
    expect_true("legacy sourcesDir removed", !normalized.at("paths").contains("sourcesDir"), "sourcesDir remains public");
    expect_true("legacy stateDir removed", !normalized.at("paths").contains("stateDir"), "stateDir remains public");
    expect_true("legacy statusRoot removed", !normalized.at("paths").contains("statusRoot"), "statusRoot remains public");
    expect_true("legacy historyRoot removed", !normalized.at("paths").contains("historyRoot"), "historyRoot remains public");
}

void test_profile_rejects_system_path_overrides() {
    Json current = valid_profile();
    current["paths"]["statusRoot"] = "/etc";
    expect_validation_error(
        "current system path",
        [&] { btrfsbackup::normalize_profile(current); },
        "paths.statusRoot is not supported"
    );

    Json legacy = valid_profile();
    legacy["schemaVersion"] = 1;
    legacy["paths"]["statusRoot"] = "/etc";
    expect_validation_error(
        "legacy system path",
        [&] { btrfsbackup::normalize_profile(legacy); },
        "paths.statusRoot is application-controlled"
    );
}

void test_profile_rejects_removed_notifications() {
    Json raw = valid_profile();
    raw["notifications"] = {{"enabled", true}};

    expect_validation_error(
        "removed notifications",
        [&] { btrfsbackup::normalize_profile(raw); },
        "profile.notifications is not supported"
    );
}

void test_profile_hooks_round_trip_as_explicit_program_arguments() {
    Json raw = valid_profile();
    raw["hooks"] = {
        {"beforeSnapshot", Json::array({
            {
                {"type", "program"},
                {"program", "/etc/btrfs-backup/hooks.d/prepare-postgresql-backup"},
                {"arguments", Json::array({"--mode", "snapshot"})},
                {"timeoutSeconds", 45}
            }
        })},
        {"afterSnapshot", Json::array({
            {
                {"type", "program"},
                {"program", "/etc/btrfs-backup/hooks.d/resume-postgresql"},
                {"arguments", Json::array()},
                {"timeoutSeconds", 30}
            }
        })}
    };

    btrfsbackup::Profile profile = btrfsbackup::profile_from_json(raw);
    Json round_trip = btrfsbackup::profile_to_json(profile);

    expect_true("before hook count", profile.hooks.before_snapshot.size() == 1, "wrong before hook count");
    expect_true("before hook program", profile.hooks.before_snapshot.at(0).program == "/etc/btrfs-backup/hooks.d/prepare-postgresql-backup", "wrong hook program");
    expect_true("before hook arg", profile.hooks.before_snapshot.at(0).arguments.at(1) == "snapshot", "wrong hook argument");
    expect_true("before hook timeout", profile.hooks.before_snapshot.at(0).timeout_seconds == 45, "wrong hook timeout");
    expect_true("after hook count", profile.hooks.after_snapshot.size() == 1, "wrong after hook count");
    expect_true("after hook timeout", profile.hooks.after_snapshot.at(0).timeout_seconds == 30, "wrong after hook timeout");
    expect_true("hook round trip", round_trip == btrfsbackup::normalize_profile(raw), "hook JSON did not round trip");
}

void test_profile_rejects_unsafe_hook_shape() {
    Json raw = valid_profile();
    raw["hooks"] = {
        {"beforeSnapshot", Json::array({
            {
                {"type", "program"},
                {"program", "/etc/btrfs-backup/hooks.d/prepare"},
                {"arguments", Json::array()}
            }
        })}
    };
    expect_validation_error("hook timeout required", [&] { btrfsbackup::normalize_profile(raw); }, "timeoutSeconds is required");

    raw = valid_profile();
    raw["hooks"] = {
        {"beforeSnapshot", Json::array({
            {
                {"type", "shell"},
                {"program", "/etc/btrfs-backup/hooks.d/prepare"},
                {"arguments", Json::array()}
            }
        })}
    };
    expect_validation_error("hook type", [&] { btrfsbackup::normalize_profile(raw); }, "type must be program");

    raw = valid_profile();
    raw["hooks"] = {
        {"beforeSnapshot", Json::array({
            {
                {"type", "program"},
                {"program", "prepare"},
                {"arguments", Json::array()},
                {"timeoutSeconds", 30}
            }
        })}
    };
    expect_validation_error("hook program absolute", [&] { btrfsbackup::normalize_profile(raw); }, "absolute path");

    raw["hooks"]["beforeSnapshot"][0]["program"] = "/home/kamil/bin/prepare";
    expect_validation_error(
        "hook program outside trusted directory",
        [&] { btrfsbackup::normalize_profile(raw); },
        "must be a direct child of /etc/btrfs-backup/hooks.d"
    );

    raw["hooks"]["beforeSnapshot"][0]["program"] = "/etc/btrfs-backup/hooks.d/postgresql/prepare";
    expect_validation_error(
        "hook program nested directory",
        [&] { btrfsbackup::normalize_profile(raw); },
        "must be a direct child of /etc/btrfs-backup/hooks.d"
    );

    raw = valid_profile();
    raw["hooks"] = {
        {"beforeSnapshot", Json::array({
            {
                {"type", "program"},
                {"program", "/etc/btrfs-backup/hooks.d/prepare"},
                {"arguments", Json::array()},
                {"timeoutSeconds", 0}
            }
        })}
    };
    expect_validation_error("hook timeout positive", [&] { btrfsbackup::normalize_profile(raw); }, "outside the supported range");
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
    Json public_profile = btrfsbackup::load_json_file(
        root / "rendered" / "var" / "lib" / "btrfs-backup" / "public" / "profiles" / "default.json"
    );
    expect_true("public target label", public_profile.at("target").at("name") == "backupdisk", "missing target label");
    expect_true("public source label", public_profile.at("sources").at(0).at("name") == "Home", "missing source label");
    expect_true("public subvolume hidden", !public_profile.at("sources").at(0).contains("subvolume"), "public profile exposes subvolume path");
    expect_true("public target device hidden", !public_profile.at("target").contains("device"), "public profile exposes device");
    expect_true("public UUID hidden", !public_profile.at("target").contains("luksUuid"), "public profile exposes UUID");
    expect_true("public paths hidden", !public_profile.contains("paths"), "public profile exposes paths");
    expect_true("public hooks hidden", !public_profile.contains("hooks"), "public profile exposes hooks");
    expect_true(
        "typed tree mount dependency",
        fs::is_regular_file(root / "rendered" / "etc" / "systemd" / "system" / "btrfs-backup@default.service.d" / "target-mount.conf"),
        "missing rendered mount dependency"
    );
    fs::remove_all(root);
}

void test_typed_store_saves_tree() {
    fs::path root = test_root();
    btrfsbackup::Profile profile = btrfsbackup::profile_from_json(valid_profile());

    btrfsbackup::save_tree(
        profile,
        root / "etc" / "btrfs-backup",
        root / "etc" / "udev" / "rules.d",
        root / "etc" / "systemd" / "system",
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
    expect_true(
        "typed save mount dependency",
        fs::is_regular_file(root / "etc" / "systemd" / "system" / "btrfs-backup@default.service.d" / "target-mount.conf"),
        "missing saved mount dependency"
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
    test_rejects_missing_btrfs_uuid();
    test_rejects_non_dev_target();
    test_rejects_nested_roots();
    test_profile_round_trips_normalized_json();
    test_profile_migrates_safe_legacy_system_paths();
    test_profile_rejects_system_path_overrides();
    test_profile_rejects_removed_notifications();
    test_profile_hooks_round_trip_as_explicit_program_arguments();
    test_profile_rejects_unsafe_hook_shape();
    test_typed_store_renders_tree();
    test_typed_store_saves_tree();
    test_render_udev_optional_matches();

    if (failures > 0) {
        return 1;
    }
    std::cout << "ok - profile module tests\n";
    return 0;
}
