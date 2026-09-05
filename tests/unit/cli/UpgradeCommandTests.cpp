// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <sstream>
#include <string>
#include <sys/stat.h>

#include <cli/upgrade/UpgradeCommand.hpp>
#include <config/json/Json.hpp>
#include <config/json/JsonIo.hpp>

#include "support/TestHelpers.hpp"

namespace fs = std::filesystem;

namespace {

btrfsbackup::config::json::Json profile(int schema_version, bool with_generation) {
    btrfsbackup::config::json::Json value{
        {"schemaVersion", schema_version},
        {"profileId", "default"},
        {"name", "Default"},
        {"enabled", true},
        {"target", {{"device", "/dev/disk/by-uuid/11111111-2222-3333-4444-555555555555"}, {"luksUuid", "11111111-2222-3333-4444-555555555555"}, {"btrfsUuid", "66666666-7777-8888-9999-aaaaaaaaaaaa"}, {"mapperName", "backupdisk"}, {"activation", {{"mode", "askPassword"}}}}},
        {"sources", btrfsbackup::config::json::Json::array({{{"id", "home"}, {"name", "Home"}, {"enabled", true}, {"subvolume", "/home"}, {"localSnapshotDir", "/.snapshots/btrfs-backup/home"}, {"remoteSubdir", "home"}, {"remoteRetention", 2}, {"localRetention", 2}}})},
    };
    if (with_generation) {
        value["configurationGeneration"] = "0123456789abcdef0123456789abcdef";
    }
    return value;
}

void install_profile(const fs::path& root, const btrfsbackup::config::json::Json& value) {
    const fs::path path = root / "profiles/default/profile.json";
    test_helpers::write_file(path, btrfsbackup::config::json::dump_json(value));
    chmod(path.c_str(), 0600);
}

void test_preflight_accepts_complete_v4_profile() {
    const fs::path root = test_helpers::test_root("upgrade-command", "ready");
    install_profile(root, profile(4, true));
    std::ostringstream output;
    std::ostringstream error;

    const int result = btrfsbackup::cli::upgrade::upgrade(root, {"preflight"}, output, error);

    test_helpers::expect_eq("upgrade preflight ready result", std::to_string(result), "0");
    test_helpers::expect_contains("upgrade preflight ready profile", output.str(), "READY default");
    test_helpers::expect_contains("upgrade preflight ready summary", output.str(), "all 1 profile(s)");
    test_helpers::expect_eq("upgrade preflight ready stderr", error.str(), "");
    fs::remove_all(root);
}

void test_preflight_blocks_legacy_and_incomplete_profiles() {
    const fs::path root = test_helpers::test_root("upgrade-command", "blocked");
    install_profile(root, profile(3, false));
    std::ostringstream output;
    std::ostringstream error;

    int result = btrfsbackup::cli::upgrade::upgrade(root, {"preflight"}, output, error);
    test_helpers::expect_eq("upgrade preflight legacy result", std::to_string(result), "1");
    test_helpers::expect_contains("upgrade preflight legacy reason", error.str(), "schema version 3");
    test_helpers::expect_contains("upgrade preflight export action", error.str(), "profile export-v4 --all");

    install_profile(root, profile(4, false));
    output.str("");
    error.str("");
    result = btrfsbackup::cli::upgrade::upgrade(root, {"preflight"}, output, error);
    test_helpers::expect_eq("upgrade preflight generation result", std::to_string(result), "1");
    test_helpers::expect_contains(
        "upgrade preflight generation reason",
        error.str(),
        "configurationGeneration is required"
    );
    fs::remove_all(root);
}

} // namespace

int main() {
    test_preflight_accepts_complete_v4_profile();
    test_preflight_blocks_legacy_and_incomplete_profiles();
    return test_helpers::finish("upgrade command tests");
}
