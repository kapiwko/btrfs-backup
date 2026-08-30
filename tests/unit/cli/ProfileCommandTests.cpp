// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <config/model/Json.hpp>
#include <config/model/JsonIo.hpp>
#include <cli/profile/ProfileCommand.hpp>
#include <config/ports/ConfigurationActivator.hpp>

#include "support/TestHelpers.hpp"

namespace fs = std::filesystem;

namespace {

fs::path test_root(const std::string& name) {
    return test_helpers::test_root("profile-command", name);
}

btrfsbackup::config::Json sample_profile_json() {
    return {
        {"schemaVersion", 3},
        {"profileId", "default"},
        {"name", "Default backup"},
        {"enabled", true},
        {"target", {{"device", "/dev/disk/by-uuid/11111111-2222-3333-4444-555555555555"}, {"luksUuid", "11111111-2222-3333-4444-555555555555"}, {"btrfsUuid", "66666666-7777-8888-9999-aaaaaaaaaaaa"}, {"mapperName", "backupdisk"}}},
        {"sources", btrfsbackup::config::Json::array({{{"id", "home"}, {"name", "Home"}, {"enabled", true}, {"subvolume", "/home"}, {"localSnapshotDir", "/.snapshots/btrfs-backup/home"}, {"remoteSubdir", "home"}, {"remoteRetention", 2}, {"localRetention", 2}}})}
    };
}

void write_installed_profile(const fs::path& root) {
    const fs::path profile = root / "profiles" / "default" / "profile.json";
    test_helpers::write_file(profile, btrfsbackup::config::dump_json(sample_profile_json()));
    fs::permissions(profile, fs::perms::owner_read | fs::perms::owner_write);
}

std::string read_file(const fs::path& path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
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
    btrfsbackup::config::Json profile = btrfsbackup::config::load_json_file(profile_json);
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

void test_profile_activation_migration_previews_key_file() {
    fs::path root = test_root("activation-preview");
    write_installed_profile(root);
    const fs::path crypttab = root / "crypttab";
    test_helpers::write_file(
        crypttab,
        "# existing configuration\n"
        "backupdisk UUID=11111111-2222-3333-4444-555555555555 /root/keys/backupdisk.key luks,noauto\n"
    );
    btrfsbackup::config::NullConfigurationActivator activator;
    std::ostringstream output;
    std::streambuf* previous = std::cout.rdbuf(output.rdbuf());

    const int result = btrfsbackup::cli::profile::profile(
        {"migrate-activation", "--profile", "default", "--crypttab", crypttab.string()},
        root,
        activator
    );

    std::cout.rdbuf(previous);
    test_helpers::expect_eq("activation preview result", std::to_string(result), "0");
    const btrfsbackup::config::Json migrated = btrfsbackup::config::Json::parse(output.str());
    test_helpers::expect_true(
        "activation preview keyfile",
        migrated.at("target").at("activation").at("mode") == "keyFile" &&
            migrated.at("target").at("activation").at("keyFile") == "/root/keys/backupdisk.key",
        "legacy key file was not migrated"
    );
    test_helpers::expect_true(
        "activation preview does not write",
        !btrfsbackup::config::load_json_file(root / "profiles" / "default" / "profile.json")
             .at("target")
             .contains("activation"),
        "preview modified the installed profile"
    );
    fs::remove_all(root);
}

void test_profile_activation_migration_applies_without_editing_crypttab() {
    fs::path root = test_root("activation-apply");
    write_installed_profile(root);
    const fs::path crypttab = root / "crypttab";
    const std::string legacy =
        "backupdisk UUID=11111111-2222-3333-4444-555555555555 none luks,noauto\n";
    test_helpers::write_file(crypttab, legacy);
    btrfsbackup::config::NullConfigurationActivator activator;
    std::ostringstream output;
    std::streambuf* previous = std::cout.rdbuf(output.rdbuf());

    const int result = btrfsbackup::cli::profile::profile(
        {
            "--udev-root",
            (root / "udev").string(),
            "--systemd-root",
            (root / "systemd").string(),
            "--public-root",
            (root / "public").string(),
            "migrate-activation",
            "--profile",
            "default",
            "--crypttab",
            crypttab.string(),
            "--apply",
        },
        root,
        activator
    );

    std::cout.rdbuf(previous);
    test_helpers::expect_eq("activation apply result", std::to_string(result), "0");
    const btrfsbackup::config::Json migrated =
        btrfsbackup::config::load_json_file(root / "profiles" / "default" / "profile.json");
    test_helpers::expect_true(
        "activation apply ask password",
        migrated.at("target").at("activation").at("mode") == "askPassword",
        "none was not migrated to askPassword"
    );
    test_helpers::expect_eq("activation apply preserves crypttab", read_file(crypttab), legacy);
    test_helpers::expect_contains(
        "activation apply output",
        output.str(),
        "legacy crypttab was not modified"
    );
    fs::remove_all(root);
}

} // namespace

int main() {
    test_profile_create_writes_json();
    test_profile_list_uses_config_root();
    test_profile_activation_migration_previews_key_file();
    test_profile_activation_migration_applies_without_editing_crypttab();

    return test_helpers::finish("profile command tests");
}
