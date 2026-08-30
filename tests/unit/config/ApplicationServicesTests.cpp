// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <fstream>
#include <string>

#include <platform/linux/config/InstallationService.hpp>
#include <platform/linux/config/ProfileActivationMigration.hpp>
#include <config/model/JsonIo.hpp>
#include <config/model/ProfileDocument.hpp>
#include <platform/linux/config/ProfileService.hpp>
#include <platform/linux/config/RenderDirectory.hpp>
#include <platform/linux/FileIo.hpp>
#include <state/StatusService.hpp>
#include <state/RunHistory.hpp>

#include "support/ValidationTestHelpers.hpp"

namespace fs = std::filesystem;

namespace {

fs::path test_root(const std::string& name) {
    return test_helpers::test_root("application-services", name);
}

std::string read_text(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

btrfsbackup::config::Profile sample_profile() {
    return btrfsbackup::config::profile_from_json({{"schemaVersion", 3}, {"profileId", "laptop"}, {"name", "Laptop backup"}, {"enabled", true}, {"target", {{"device", "/dev/disk/by-uuid/11111111-2222-3333-4444-555555555555"}, {"luksUuid", "11111111-2222-3333-4444-555555555555"}, {"btrfsUuid", "66666666-7777-8888-9999-aaaaaaaaaaaa"}, {"mapperName", "backupdisk"}}}, {"sources", btrfsbackup::config::Json::array({{{"id", "home"}, {"name", "Home"}, {"enabled", true}, {"subvolume", "/home"}, {"localSnapshotDir", "/.snapshots/btrfs-backup/home"}, {"remoteSubdir", "home"}, {"remoteRetention", 7}, {"localRetention", 3}}})}});
}

void test_profile_and_installation_use_cases() {
    fs::path root = test_root("profile-installation");
    fs::path profile_file = root / "profiles" / "laptop" / "profile.json";
    btrfsbackup::platform::linux::write_profile_file(sample_profile(), profile_file);

    btrfsbackup::config::Profile loaded = btrfsbackup::platform::linux::validate_profile_file(profile_file);
    test_helpers::expect_eq("validated profile", std::string(loaded.id.value()), "laptop");
    auto profiles = btrfsbackup::platform::linux::list_profiles(root / "profiles");
    test_helpers::expect_eq("listed profile", profiles.at(0), "laptop");

    btrfsbackup::platform::linux::render_installation({profile_file, root / "rendered", {}});
    test_helpers::expect_true(
        "rendered installation",
        fs::is_regular_file(root / "rendered" / "systemd" / "mnt-btrfs\\x2dbackup-laptop.mount"),
        "native target mount was not rendered"
    );
    test_helpers::expect_true(
        "rendered installation has no table fragments",
        !fs::exists(root / "rendered" / "config" / "fstab.fragment") &&
            !fs::exists(root / "rendered" / "config" / "crypttab.fragment"),
        "legacy table fragments were rendered"
    );
    fs::remove_all(root);
}

void test_activation_migration_rejects_unsupported_crypttab_semantics() {
    fs::path root = test_root("activation-migration-options");
    const fs::path crypttab = root / "crypttab";
    test_helpers::write_file(
        crypttab,
        "backupdisk UUID=11111111-2222-3333-4444-555555555555 none luks,keyscript=/usr/local/bin/key\n"
    );
    test_helpers::expect_validation_error(
        "activation migration unsupported options",
        [&] {
            (void)btrfsbackup::platform::linux::migrate_target_activation_from_crypttab(
                sample_profile(),
                crypttab
            );
        },
        "keyscript="
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

    auto current = btrfsbackup::state::poll_status(root / "status", "laptop", "");
    test_helpers::expect_true("polled status", current.has_value(), "current status was not returned");
    test_helpers::expect_true("parsed status", current->data.at("state") == "running", "wrong status state");
    auto history = btrfsbackup::state::get_status_history(root / "history", "laptop", 10);
    test_helpers::expect_eq("status history size", std::to_string(history.size()), "1");
    fs::remove_all(root);
}

void test_profile_render_replaces_only_owned_render_directories() {
    fs::path root = test_root("profile-render-safety");
    fs::path profile_file = root / "profile.json";
    btrfsbackup::platform::linux::write_profile_file(sample_profile(), profile_file);

    fs::path unmarked = root / "home-like";
    test_helpers::write_file(unmarked / "important.txt", "keep me");
    test_helpers::expect_validation_error(
        "render refuses unmarked directory",
        [&] { btrfsbackup::platform::linux::render_profile(profile_file, unmarked); },
        "without .btrfs-backup-render-root"
    );
    test_helpers::expect_eq(
        "render preserves unmarked file",
        read_text(unmarked / "important.txt"),
        "keep me"
    );

    fs::create_directories(root / "real-parent");
    fs::create_directory_symlink(root / "real-parent", root / "linked-parent");
    test_helpers::expect_validation_error(
        "render rejects symlink parent",
        [&] { btrfsbackup::platform::linux::render_profile(profile_file, root / "linked-parent" / "rendered"); },
        "parent is not a directory"
    );
    test_helpers::expect_true(
        "render does not follow symlink parent",
        !fs::exists(root / "real-parent" / "rendered"),
        "render followed a symlink parent"
    );

    fs::path rendered = root / "rendered";
    fs::create_directories(rendered);
    btrfsbackup::platform::linux::render_profile(profile_file, rendered);
    test_helpers::expect_true(
        "render marker",
        fs::is_regular_file(rendered / btrfsbackup::platform::linux::render_root_marker),
        "render root marker was not created"
    );
    test_helpers::write_file(rendered / "stale.txt", "old");
    btrfsbackup::platform::linux::render_profile(profile_file, rendered);
    test_helpers::expect_true(
        "render removes owned stale file",
        !fs::exists(rendered / "stale.txt"),
        "stale file survived"
    );

    const fs::path rendered_profile = rendered / "etc" / "btrfs-backup" / "profiles" / "laptop" / "profile.json";
    const std::string before_failure = read_text(rendered_profile);
    test_helpers::expect_validation_error(
        "render validation failure",
        [&] {
            btrfsbackup::platform::linux::replace_render_directory(
                rendered,
                [](const fs::path& staging) {
                    btrfsbackup::platform::linux::atomic_write(staging / "candidate.txt", "candidate", 0600);
                },
                [](const fs::path&) {
                    throw btrfsbackup::ValidationError("injected rendered tree validation failure");
                }
            );
        },
        "injected rendered tree validation failure"
    );
    test_helpers::expect_eq(
        "render validation preserves previous result",
        read_text(rendered_profile),
        before_failure
    );
    test_helpers::expect_true(
        "render validation hides candidate",
        !fs::exists(rendered / "candidate.txt"),
        "invalid staged render was published"
    );

    test_helpers::write_file(root / "invalid.json", "{}\n");
    test_helpers::expect_validation_error(
        "render validates before replacement",
        [&] { btrfsbackup::platform::linux::render_profile(root / "invalid.json", rendered); },
        "schemaVersion"
    );
    test_helpers::expect_eq(
        "render preserves previous result",
        read_text(rendered_profile),
        before_failure
    );
    fs::remove_all(root);
}

} // namespace

int main() {
    test_profile_and_installation_use_cases();
    test_activation_migration_rejects_unsupported_crypttab_semantics();
    test_profile_render_replaces_only_owned_render_directories();
    test_status_use_cases();
    return test_helpers::finish("application services tests");
}
