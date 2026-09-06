// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <sys/stat.h>

#include <config/json/Json.hpp>
#include <config/json/JsonIo.hpp>
#include <cli/profile/ProfileCommand.hpp>
#include <config/ports/ConfigurationActivator.hpp>

#include "support/TestHelpers.hpp"

namespace fs = std::filesystem;

namespace {

fs::path test_root(const std::string& name) {
    return test_helpers::test_root("profile-command", name);
}

btrfsbackup::config::json::Json sample_profile_json() {
    return {
        {"schemaVersion", 4},
        {"profileId", "default"},
        {"name", "Default backup"},
        {"enabled", true},
        {"target", {{"device", "/dev/disk/by-uuid/11111111-2222-3333-4444-555555555555"}, {"luksUuid", "11111111-2222-3333-4444-555555555555"}, {"btrfsUuid", "66666666-7777-8888-9999-aaaaaaaaaaaa"}, {"mapperName", "backupdisk"}, {"activation", {{"mode", "askPassword"}}}}},
        {"sources", btrfsbackup::config::json::Json::array({{{"id", "home"}, {"name", "Home"}, {"enabled", true}, {"subvolume", "/home"}, {"localSnapshotDir", "/.snapshots/btrfs-backup/home"}, {"remoteSubdir", "home"}, {"remoteRetention", 2}, {"localRetention", 2}}})}
    };
}

void test_profile_create_writes_json() {
    fs::path root = test_root("profile-create");
    fs::path profile_json = root / "profile.json";

    btrfsbackup::config::NullConfigurationActivator activator;
    const std::vector<std::string> args{
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
    };
    int result = btrfsbackup::cli::profile::profile(args, "/etc/btrfs-backup/profiles.d", activator);

    test_helpers::expect_eq("profile create result", std::to_string(result), "0");
    btrfsbackup::config::json::Json profile = btrfsbackup::config::json::load_json_file(profile_json);
    test_helpers::expect_true("profile create schema", profile.at("schemaVersion") == 4, "wrong profile schema version");
    test_helpers::expect_eq("profile create id", profile.at("profileId").get<std::string>(), "default");
    test_helpers::expect_eq("profile create source id", profile.at("sources").at(0).at("id").get<std::string>(), "home");
    test_helpers::expect_eq("profile create remote root", profile.at("paths").at("remoteRoot").get<std::string>(), "/mnt/btrfs-backup/default/snapshots");
    test_helpers::expect_true("profile create hides mount point", !profile.at("target").contains("mountPoint"), "mount point leaked into profile");
    for (const std::string& key : {"sourcesDir", "stateDir", "statusRoot", "historyRoot"}) {
        test_helpers::expect_true("profile create hides " + key, !profile.at("paths").contains(key), "system path leaked into profile");
    }
    fs::remove_all(root);
}

void test_profile_list_uses_config_root() {
    fs::path root = test_root("profile-list");
    test_helpers::write_file(root / "profiles" / "default" / "profile.json", "{}\n");
    btrfsbackup::config::NullConfigurationActivator activator;
    std::ostringstream output;
    std::streambuf* previous = std::cout.rdbuf(output.rdbuf());

    const int result = btrfsbackup::cli::profile::profile({"list"}, root, activator);

    std::cout.rdbuf(previous);
    test_helpers::expect_eq("profile list result", std::to_string(result), "0");
    test_helpers::expect_eq("profile list config root", output.str(), "default\n");
    fs::remove_all(root);
}

void test_profile_regenerate_all_restores_derived_artifacts() {
    const fs::path root = test_root("profile-regenerate-all");
    const fs::path etc_root = root / "etc/btrfs-backup";
    const fs::path udev_root = root / "etc/udev/rules.d";
    const fs::path systemd_root = root / "etc/systemd/system";
    const fs::path public_root = root / "var/lib/btrfs-backup/public/profiles";
    const auto command = [&](std::vector<std::string> arguments) {
        arguments.insert(arguments.begin(), {
                                                "--etc-root",
                                                etc_root.string(),
                                                "--udev-root",
                                                udev_root.string(),
                                                "--systemd-root",
                                                systemd_root.string(),
                                                "--public-root",
                                                public_root.string(),
                                            });
        btrfsbackup::config::NullConfigurationActivator activator;
        return btrfsbackup::cli::profile::profile(arguments, etc_root, activator);
    };

    auto save_profile = [&](const std::string& id) {
        auto profile = sample_profile_json();
        profile["profileId"] = id;
        profile["name"] = id + " backup";
        const fs::path draft = root / (id + ".json");
        test_helpers::write_file(draft, btrfsbackup::config::json::dump_json(profile));
        test_helpers::expect_eq(
            "initial profile save " + id,
            std::to_string(command({"save", "--file", draft.string()})),
            "0"
        );
    };

    save_profile("archive");
    save_profile("default");
    const std::string archive_generation = btrfsbackup::config::json::load_json_file(
                                               etc_root / "profiles/archive/profile.json"
    )
                                               .at("configurationGeneration")
                                               .get<std::string>();
    fs::remove(udev_root / "99-btrfs-backup-archive.rules");
    fs::remove(systemd_root / "btrfs-backup@default.service.d/target-mount.conf");

    std::ostringstream output;
    std::streambuf* previous = std::cout.rdbuf(output.rdbuf());
    const int result = command({"regenerate", "--all"});
    std::cout.rdbuf(previous);

    test_helpers::expect_eq("profile regenerate result", std::to_string(result), "0");
    test_helpers::expect_eq(
        "profile regenerate output",
        output.str(),
        "Regenerated profile archive\nRegenerated profile default\n"
    );
    test_helpers::expect_true(
        "profile regenerate restores udev rule",
        fs::is_regular_file(udev_root / "99-btrfs-backup-archive.rules"),
        "archive udev rule was not restored"
    );
    test_helpers::expect_true(
        "profile regenerate restores systemd drop-in",
        fs::is_regular_file(systemd_root / "btrfs-backup@default.service.d/target-mount.conf"),
        "default systemd drop-in was not restored"
    );
    const std::string regenerated_generation = btrfsbackup::config::json::load_json_file(
                                                   etc_root / "profiles/archive/profile.json"
    )
                                                   .at("configurationGeneration")
                                                   .get<std::string>();
    test_helpers::expect_true(
        "profile regenerate updates generation",
        regenerated_generation != archive_generation,
        "configuration generation did not change"
    );
    fs::remove_all(root);
}


} // namespace

int main() {
    test_profile_create_writes_json();
    test_profile_list_uses_config_root();
    test_profile_regenerate_all_restores_derived_artifacts();
    return test_helpers::finish("profile command tests");
}
