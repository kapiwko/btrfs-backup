// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <array>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <unistd.h>

#include <config/errors.hpp>
#include <config/json.hpp>
#include <config/json_io.hpp>
#include <config/profile.hpp>
#include <config/profile_loader.hpp>
#include <config/profile_render.hpp>
#include <config/profile_store.hpp>
#include <platform/linux/file_io.hpp>
#include <platform/linux/file_lock.hpp>

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
        {"schemaVersion", 3},
        {"profileId", "default"},
        {"name", "Default backup"},
        {"enabled", true},
        {"target", {
            {"device", "/dev/disk/by-uuid/11111111-2222-3333-4444-555555555555"},
            {"luksUuid", "11111111-2222-3333-4444-555555555555"},
            {"btrfsUuid", "66666666-7777-8888-9999-aaaaaaaaaaaa"},
            {"partitionUuid", "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee"},
            {"serial", "SERIAL_123"},
            {"mapperName", "backupdisk"}
        }},
        {"paths", {
            {"remoteRoot", "/mnt/btrfs-backup/default/snapshots"},
            {"incomingRoot", "/mnt/btrfs-backup/default/.incoming"}
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

std::string read_text(const fs::path& path) {
    std::ifstream stream(path);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void test_rejects_bad_uuid() {
    Json profile = valid_profile();
    profile["target"]["luksUuid"] = "bad";
    expect_validation_error("bad uuid", [&] { btrfsbackup::normalize_profile(profile); }, "target.luksUuid");
}

void test_rejects_bad_configuration_generation() {
    Json profile = valid_profile();
    profile["configurationGeneration"] = "NOT-A-GENERATION";
    expect_validation_error(
        "bad configuration generation",
        [&] { (void)btrfsbackup::normalize_profile(profile); },
        "configurationGeneration"
    );
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
    profile["paths"]["incomingRoot"] = "/mnt/btrfs-backup/default/snapshots/.incoming";
    expect_validation_error("nested roots", [&] { btrfsbackup::normalize_profile(profile); }, "remoteRoot and paths.incomingRoot");
}

void test_profile_round_trips_normalized_json() {
    Json normalized = btrfsbackup::normalize_profile(valid_profile());
    btrfsbackup::Profile profile = btrfsbackup::profile_from_json(normalized);
    Json round_trip = btrfsbackup::profile_to_json(profile);

    expect_true("profile model id", profile.id == btrfsbackup::ProfileId{"default"}, "wrong profile id");
    expect_true("profile model source", profile.sources.size() == 1 && profile.sources.at(0).id == btrfsbackup::SourceId{"home"}, "wrong profile source");
    expect_true("profile model round trip", round_trip == normalized, "typed profile did not preserve normalized JSON");
}

void test_profile_migrates_safe_legacy_system_paths() {
    Json legacy = valid_profile();
    legacy["schemaVersion"] = 1;
    legacy["target"]["mountPoint"] = "/mnt/btrfs-backup/default";
    legacy["paths"]["sourcesDir"] = "/etc/btrfs-backup/profiles/default/sources.d";
    legacy["paths"]["stateDir"] = "/var/lib/btrfs-backup";
    legacy["paths"]["statusRoot"] = "/run/btrfs-backup/profiles";
    legacy["paths"]["historyRoot"] = "/var/lib/btrfs-backup/history";

    Json normalized = btrfsbackup::normalize_profile(legacy);
    expect_true("legacy migrated schema", normalized.at("schemaVersion") == 3, "legacy profile was not migrated");
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
    legacy["target"]["mountPoint"] = "/mnt/btrfs-backup/default";
    legacy["paths"]["statusRoot"] = "/etc";
    expect_validation_error(
        "legacy system path",
        [&] { btrfsbackup::normalize_profile(legacy); },
        "paths.statusRoot is application-controlled"
    );
}

void test_mount_point_is_application_controlled() {
    Json raw = valid_profile();
    raw["target"]["mountPoint"] = "/home/alice/backup";
    expect_validation_error("profile mount point rejected", [&] {
        (void)btrfsbackup::normalize_profile(raw);
    }, "application-controlled");

    Json custom = valid_profile();
    custom["paths"] = Json::object();
    btrfsbackup::Profile profile = btrfsbackup::profile_from_json(custom, "/srv/backup-targets");
    expect_true(
        "custom mount root",
        profile.target.mount_point == "/srv/backup-targets/default",
        "profile mount point was not derived from the application root"
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

    const fs::path profile_path = root / "etc" / "btrfs-backup" / "profiles" / "default" / "profile.json";
    const fs::path public_path = root / "public" / "default.json";
    Json saved_profile = btrfsbackup::load_json_file(profile_path);
    Json public_profile = btrfsbackup::load_json_file(public_path);
    const std::string generation = saved_profile.at("configurationGeneration").get<std::string>();
    expect_true("typed save generation length", generation.size() == 32, "invalid configuration generation");
    expect_true(
        "typed save public generation",
        public_profile.at("configurationGeneration") == generation,
        "public profile generation mismatch"
    );

    std::ifstream udev_stream(root / "etc" / "udev" / "rules.d" / "99-btrfs-backup-default.rules");
    std::string udev{std::istreambuf_iterator<char>(udev_stream), std::istreambuf_iterator<char>()};
    expect_true("typed save udev generation", udev.find(generation) != std::string::npos, "udev generation mismatch");

    std::ifstream systemd_stream(
        root / "etc" / "systemd" / "system" / "btrfs-backup@default.service.d" / "target-mount.conf"
    );
    std::string systemd{std::istreambuf_iterator<char>(systemd_stream), std::istreambuf_iterator<char>()};
    expect_true(
        "typed save systemd generation",
        systemd.find("BTRFS_BACKUP_CONFIGURATION_GENERATION=" + generation) != std::string::npos,
        "systemd generation mismatch"
    );

    const char* previous_generation_env = std::getenv("BTRFS_BACKUP_CONFIGURATION_GENERATION");
    const std::optional<std::string> previous_generation = previous_generation_env == nullptr
        ? std::nullopt
        : std::optional<std::string>(previous_generation_env);
    setenv("BTRFS_BACKUP_CONFIGURATION_GENERATION", generation.c_str(), 1);
    expect_true(
        "typed load matching generation",
        btrfsbackup::load_profile_by_id(root / "etc" / "btrfs-backup", "default").id == btrfsbackup::ProfileId{"default"},
        "matching generation was rejected"
    );
    setenv("BTRFS_BACKUP_CONFIGURATION_GENERATION", "00000000000000000000000000000000", 1);
    expect_validation_error(
        "typed load mismatched generation",
        [&] { (void)btrfsbackup::load_profile_by_id(root / "etc" / "btrfs-backup", "default"); },
        "generation does not match"
    );
    if (previous_generation.has_value()) {
        setenv("BTRFS_BACKUP_CONFIGURATION_GENERATION", previous_generation->c_str(), 1);
    } else {
        unsetenv("BTRFS_BACKUP_CONFIGURATION_GENERATION");
    }
    fs::remove_all(root);
}

void test_save_tree_staging_failure_preserves_installed_artifacts() {
    fs::path root = test_root();
    btrfsbackup::Profile original = btrfsbackup::profile_from_json(valid_profile());
    const fs::path etc_root = root / "etc" / "btrfs-backup";
    const fs::path udev_root = root / "etc" / "udev" / "rules.d";
    const fs::path systemd_root = root / "etc" / "systemd" / "system";
    const fs::path public_root = root / "public";
    btrfsbackup::save_tree(original, etc_root, udev_root, systemd_root, public_root);

    const fs::path profile_path = etc_root / "profiles" / "default" / "profile.json";
    const fs::path udev_path = udev_root / "99-btrfs-backup-default.rules";
    const fs::path systemd_path = systemd_root / "btrfs-backup@default.service.d" / "target-mount.conf";
    const fs::path public_path = public_root / "default.json";
    const std::array<std::string, 4> before{
        read_text(profile_path),
        read_text(udev_path),
        read_text(systemd_path),
        read_text(public_path),
    };
    btrfsbackup::Profile changed = original;
    changed.name = "Changed profile";
    const fs::path blocked_public_root = root / "blocked-public";
    btrfsbackup::atomic_write(blocked_public_root, "not a directory", 0600);

    bool rejected = false;
    try {
        btrfsbackup::save_tree(changed, etc_root, udev_root, systemd_root, blocked_public_root);
    } catch (const btrfsbackup::ConfigurationSaveError& error) {
        rejected = true;
        expect_true(
            "transaction staging failure code",
            error.error_code == "configuration.save_failed",
            "unexpected error code: " + error.error_code
        );
        expect_true(
            "transaction staging rollback complete",
            error.rollback_result.complete && error.rollback_result.errors.empty(),
            "staging failure unexpectedly required rollback"
        );
    } catch (const std::exception& error) {
        fail("transaction staging failure", std::string("unexpected exception: ") + error.what());
    }
    expect_true("transaction staging failure", rejected, "save unexpectedly succeeded");
    expect_true(
        "transaction preserves profile",
        read_text(profile_path) == before[0],
        "profile changed after staging failure"
    );
    expect_true("transaction preserves udev", read_text(udev_path) == before[1], "udev changed after staging failure");
    expect_true("transaction preserves systemd", read_text(systemd_path) == before[2], "systemd changed after staging failure");
    expect_true("transaction preserves public", read_text(public_path) == before[3], "public profile changed after staging failure");
    fs::remove_all(root);
}

void test_save_tree_activation_failure_rolls_back_all_artifacts() {
    fs::path root = test_root();
    btrfsbackup::Profile original = btrfsbackup::profile_from_json(valid_profile());
    const fs::path etc_root = root / "etc" / "btrfs-backup";
    const fs::path udev_root = root / "etc" / "udev" / "rules.d";
    const fs::path systemd_root = root / "etc" / "systemd" / "system";
    const fs::path public_root = root / "public";
    const fs::path profile_path = etc_root / "profiles" / "default" / "profile.json";
    const fs::path udev_path = udev_root / "99-btrfs-backup-default.rules";
    const fs::path systemd_path = systemd_root / "btrfs-backup@default.service.d" / "target-mount.conf";
    const fs::path public_path = public_root / "default.json";
    btrfsbackup::save_tree(original, etc_root, udev_root, systemd_root, public_root);

    const std::array<std::string, 4> before{
        read_text(profile_path),
        read_text(udev_path),
        read_text(systemd_path),
        read_text(public_path),
    };
    btrfsbackup::Profile changed = original;
    changed.name = "Changed profile";
    int activation_calls = 0;
    bool public_marker_was_old_during_activation = false;
    expect_validation_error(
        "transaction activation failure",
        [&] {
            btrfsbackup::save_tree(
                changed,
                etc_root,
                udev_root,
                systemd_root,
                public_root,
                [&] {
                    ++activation_calls;
                    public_marker_was_old_during_activation = read_text(public_path) == before[3];
                    if (activation_calls == 1) {
                        throw ValidationError("injected activation failure");
                    }
                }
            );
        },
        "injected activation failure"
    );

    expect_true("transaction reloads rollback", activation_calls == 2, "old configuration was not reactivated");
    expect_true(
        "transaction public marker commits last",
        public_marker_was_old_during_activation,
        "public profile was published before activation"
    );
    expect_true("transaction restores private profile", read_text(profile_path) == before[0], "private profile changed");
    expect_true("transaction restores udev rule", read_text(udev_path) == before[1], "udev rule changed");
    expect_true("transaction restores systemd drop-in", read_text(systemd_path) == before[2], "systemd drop-in changed");
    expect_true("transaction preserves public marker", read_text(public_path) == before[3], "public profile changed");
    fs::remove_all(root);
}

void test_save_tree_reports_incomplete_rollback() {
    fs::path root = test_root();
    btrfsbackup::Profile original = btrfsbackup::profile_from_json(valid_profile());
    const fs::path etc_root = root / "etc" / "btrfs-backup";
    const fs::path udev_root = root / "etc" / "udev" / "rules.d";
    const fs::path systemd_root = root / "etc" / "systemd" / "system";
    const fs::path public_root = root / "public";
    const fs::path udev_path = udev_root / "99-btrfs-backup-default.rules";
    btrfsbackup::save_tree(original, etc_root, udev_root, systemd_root, public_root);

    btrfsbackup::Profile changed = original;
    changed.name = "Changed profile";
    int activation_calls = 0;
    try {
        btrfsbackup::save_tree(
            changed,
            etc_root,
            udev_root,
            systemd_root,
            public_root,
            [&] {
                ++activation_calls;
                if (activation_calls != 1) {
                    return;
                }
                fs::remove(udev_path);
                fs::create_directory(udev_path);
                btrfsbackup::atomic_write(udev_path / "blocker", "injected rollback failure", 0600);
                throw ValidationError("injected save failure");
            }
        );
        fail("transaction incomplete rollback", "save unexpectedly succeeded");
    } catch (const btrfsbackup::ConfigurationSaveError& error) {
        expect_true(
            "transaction rollback error code",
            error.error_code == "configuration.rollback_incomplete",
            "unexpected error code: " + error.error_code
        );
        expect_true(
            "transaction rollback retains primary error",
            std::string(error.what()).find("configuration.save_failed: injected save failure") != std::string::npos,
            "primary save failure was lost"
        );
        expect_true(
            "transaction rollback reports secondary error",
            std::string(error.what()).find("configuration.rollback_incomplete") != std::string::npos,
            "rollback failure was not reported"
        );
        expect_true(
            "transaction rollback result incomplete",
            !error.rollback_result.complete && !error.rollback_result.errors.empty(),
            "rollback result contains no diagnostics"
        );
        bool reported_restore_failure = false;
        for (const btrfsbackup::RollbackError& rollback_error : error.rollback_result.errors) {
            if (rollback_error.operation == "restore previous artifact"
                && rollback_error.path.filename().string().starts_with(
                    ".99-btrfs-backup-default.rules.previous-"
                )) {
                reported_restore_failure = true;
            }
        }
        expect_true(
            "transaction rollback identifies failed artifact",
            reported_restore_failure,
            "rollback diagnostics do not identify the unrestored udev rule"
        );
    } catch (const std::exception& error) {
        fail("transaction incomplete rollback", std::string("unexpected exception: ") + error.what());
    }

    bool previous_preserved = false;
    std::error_code scan_error;
    for (const fs::directory_entry& entry : fs::directory_iterator(udev_root, scan_error)) {
        const std::string filename = entry.path().filename().string();
        if (filename.starts_with(".99-btrfs-backup-default.rules.previous-")) {
            previous_preserved = fs::is_regular_file(entry.path());
        }
    }
    expect_true(
        "transaction preserves failed rollback artifact",
        previous_preserved,
        "recoverable previous artifact was removed"
    );
    fs::remove_all(root);
}

void test_save_tree_refuses_active_profile_lock() {
    fs::path root = test_root();
    btrfsbackup::Profile original = btrfsbackup::profile_from_json(valid_profile());
    const fs::path etc_root = root / "etc" / "btrfs-backup";
    const fs::path udev_root = root / "etc" / "udev" / "rules.d";
    const fs::path systemd_root = root / "etc" / "systemd" / "system";
    const fs::path public_root = root / "public";
    const fs::path profile_path = etc_root / "profiles" / "default" / "profile.json";
    btrfsbackup::save_tree(original, etc_root, udev_root, systemd_root, public_root);
    const std::string before = read_text(profile_path);

    btrfsbackup::FileLock lock(
        btrfsbackup::profile_lock_path(etc_root / ".locks", "default")
    );
    expect_true("transaction test lock acquired", lock.try_acquire(), "cannot acquire test profile lock");
    btrfsbackup::Profile changed = original;
    changed.name = "Changed profile";
    expect_validation_error(
        "transaction active profile lock",
        [&] { btrfsbackup::save_tree(changed, etc_root, udev_root, systemd_root, public_root); },
        "profile is active"
    );
    expect_true("transaction lock preserves profile", read_text(profile_path) == before, "profile changed while active");
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
    test_rejects_bad_configuration_generation();
    test_rejects_missing_btrfs_uuid();
    test_rejects_non_dev_target();
    test_rejects_nested_roots();
    test_profile_round_trips_normalized_json();
    test_profile_migrates_safe_legacy_system_paths();
    test_profile_rejects_system_path_overrides();
    test_mount_point_is_application_controlled();
    test_profile_rejects_removed_notifications();
    test_profile_hooks_round_trip_as_explicit_program_arguments();
    test_profile_rejects_unsafe_hook_shape();
    test_typed_store_renders_tree();
    test_typed_store_saves_tree();
    test_save_tree_staging_failure_preserves_installed_artifacts();
    test_save_tree_activation_failure_rolls_back_all_artifacts();
    test_save_tree_reports_incomplete_rollback();
    test_save_tree_refuses_active_profile_lock();
    test_render_udev_optional_matches();

    if (failures > 0) {
        return 1;
    }
    std::cout << "ok - profile module tests\n";
    return 0;
}
