// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <string>
#include <vector>

#include <config/json.hpp>
#include <config/json_io.hpp>
#include <cli/profile_command.hpp>
#include <config/ports/configuration_activator.hpp>

#include "support/test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

fs::path test_root(const std::string& name) {
    return test_helpers::test_root("profile-command", name);
}

void test_profile_create_writes_json() {
    fs::path root = test_root("profile-create");
    fs::path profile_json = root / "profile.json";

    btrfsbackup::NullConfigurationActivator activator;
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
    int result = btrfsbackup::command::profile(args, "/etc/btrfs-backup/profiles.d", activator);

    test_helpers::expect_eq("profile create result", std::to_string(result), "0");
    btrfsbackup::Json profile = btrfsbackup::load_json_file(profile_json);
    test_helpers::expect_true("profile create schema", profile.at("schemaVersion") == 3, "wrong profile schema version");
    test_helpers::expect_eq("profile create id", profile.at("profileId").get<std::string>(), "default");
    test_helpers::expect_eq("profile create source id", profile.at("sources").at(0).at("id").get<std::string>(), "home");
    test_helpers::expect_eq("profile create remote root", profile.at("paths").at("remoteRoot").get<std::string>(), "/mnt/btrfs-backup/default/snapshots");
    test_helpers::expect_true("profile create hides mount point", !profile.at("target").contains("mountPoint"), "mount point leaked into profile");
    for (const std::string& key : {"sourcesDir", "stateDir", "statusRoot", "historyRoot"}) {
        test_helpers::expect_true("profile create hides " + key, !profile.at("paths").contains(key), "system path leaked into profile");
    }
    fs::remove_all(root);
}

} // namespace

int main() {
    test_profile_create_writes_json();

    return test_helpers::finish("profile command tests");
}
