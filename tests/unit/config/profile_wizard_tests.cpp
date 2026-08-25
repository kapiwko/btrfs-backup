// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <config/render_directory.hpp>
#include <config/wizard/profile_wizard_install.hpp>
#include <config/wizard/profile_wizard_model.hpp>
#include <config/wizard/profile_wizard_prompt.hpp>
#include <config/wizard/profile_wizard_sources.hpp>

#include "support/test_helpers.hpp"

namespace {

namespace fs = std::filesystem;

std::string read_file(const fs::path& path) {
    std::ifstream stream(path);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

void test_prompt_parsing() {
    test_helpers::expect_eq("trim", btrfsbackup::wizard::trim_text("  value \n"), "value");
    test_helpers::expect_true("bool true", btrfsbackup::wizard::parse_bool(" yes "), "yes should parse as true");
    test_helpers::expect_true("bool false", !btrfsbackup::wizard::parse_bool("OFF"), "OFF should parse as false");
    test_helpers::expect_eq("uint", std::to_string(btrfsbackup::wizard::parse_uint(" 42 ")), "42");
    test_helpers::expect_validation_error("invalid bool", [] {
        (void)btrfsbackup::wizard::parse_bool("maybe");
    }, "enter true or false");
    test_helpers::expect_validation_error("invalid uint", [] {
        (void)btrfsbackup::wizard::parse_uint("-1");
    }, "non-negative integer");
}

void test_prompt_defaults_and_retry() {
    std::istringstream input("\ninvalid\nfalse\nbad\n17\n");
    std::ostringstream output;

    test_helpers::expect_eq("prompt default", btrfsbackup::wizard::prompt_value(input, output, "Name", "default"), "default");
    test_helpers::expect_true("prompt bool retry", !btrfsbackup::wizard::prompt_bool(input, output, "Enabled", true), "false should be accepted after retry");
    test_helpers::expect_eq("prompt uint retry", std::to_string(btrfsbackup::wizard::prompt_uint(input, output, "Count", 3)), "17");
    test_helpers::expect_contains("prompt retry output", output.str(), "enter true or false");
    test_helpers::expect_contains("prompt uint retry output", output.str(), "enter a non-negative integer");
}

void test_source_names() {
    test_helpers::expect_eq("root source name", btrfsbackup::wizard::source_name_from_path("/"), "root");
    test_helpers::expect_eq("home source name", btrfsbackup::wizard::source_name_from_path("/home"), "home");
    test_helpers::expect_eq("sanitized source name", btrfsbackup::wizard::source_name_from_path("/mnt/My Data"), "My-Data");
    test_helpers::expect_eq("prefixed source name", btrfsbackup::wizard::source_name_from_path("/mnt/-data"), "source--data");
}

void test_source_selection() {
    std::vector<std::string> candidates{"/", "/home", "/srv"};
    test_helpers::expect_eq("default source selection", btrfsbackup::wizard::default_source_selection(candidates), "1,2");
    test_helpers::expect_eq("fallback source selection", btrfsbackup::wizard::default_source_selection({"/srv"}), "1");

    auto all = btrfsbackup::wizard::selected_sources_from_input(candidates, "a");
    test_helpers::expect_eq("all source count", std::to_string(all.size()), "3");

    auto selected = btrfsbackup::wizard::selected_sources_from_input(candidates, "2, 1,2");
    test_helpers::expect_eq("selected source count", std::to_string(selected.size()), "2");
    test_helpers::expect_eq("selected first", selected.at(0), "/home");
    test_helpers::expect_eq("selected second", selected.at(1), "/");

    test_helpers::expect_validation_error("source selection token", [&] {
        (void)btrfsbackup::wizard::selected_sources_from_input(candidates, "x");
    }, "invalid source selection");
    test_helpers::expect_validation_error("source selection range", [&] {
        (void)btrfsbackup::wizard::selected_sources_from_input(candidates, "4");
    }, "out of range");
}

btrfsbackup::ProfileWizardAnswers sample_answers() {
    btrfsbackup::ProfileWizardAnswers answers;
    answers.profile_id = "laptop";
    answers.profile_name = "Laptop backup";
    answers.target_device = "/dev/disk/by-uuid/AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE";
    answers.target_luks_uuid = "AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE";
    answers.target_btrfs_uuid = "11111111-2222-3333-4444-555555555555";
    answers.target_partition_uuid = "99999999-8888-7777-6666-555555555555";
    answers.target_serial = "serial-1";
    answers.target_mapper_name = "backupdisk";
    answers.sources = {
        {
            .id = "root",
            .subvolume = "/",
            .local_snapshot_dir = "/.snapshots/btrfs-backup/root",
            .remote_subdir = "root",
        },
        {
            .id = "home",
            .subvolume = "/home",
            .local_snapshot_dir = "/.snapshots/btrfs-backup/home",
            .remote_subdir = "home",
        },
    };
    answers.remote_retention = 45;
    answers.local_retention = 12;
    answers.daily_limit = false;
    answers.incremental_required = true;
    answers.keep_failed_local_snapshot = true;
    answers.auto_eject = false;
    answers.minimum_target_free_bytes = 1000;
    answers.minimum_local_free_bytes = 2000;
    answers.keyfile = "none";
    return answers;
}

void test_profile_from_wizard_answers() {
    btrfsbackup::Profile profile = btrfsbackup::profile_from_wizard_answers(sample_answers());

    test_helpers::expect_eq("wizard profile id", profile.id, "laptop");
    test_helpers::expect_eq("wizard profile name", profile.name, "Laptop backup");
    test_helpers::expect_eq("wizard target device", profile.target.device, "/dev/disk/by-uuid/AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE");
    test_helpers::expect_eq("wizard target luks uuid lower", profile.target.luks_uuid, "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee");
    test_helpers::expect_eq("wizard target mount unit", profile.target.mount_unit, "mnt-btrfs\\x2dbackup-laptop.mount");
    test_helpers::expect_eq("wizard remote root", profile.paths.remote_root, "/mnt/btrfs-backup/laptop/snapshots");
    test_helpers::expect_eq("wizard incoming root", profile.paths.incoming_root, "/mnt/btrfs-backup/laptop/.incoming");
    test_helpers::expect_eq("wizard source count", std::to_string(profile.sources.size()), "2");
    test_helpers::expect_eq("wizard first source id", profile.sources.at(0).id, "root");
    test_helpers::expect_eq("wizard second source subvolume", profile.sources.at(1).subvolume, "/home");
    test_helpers::expect_eq("wizard source remote retention", std::to_string(profile.sources.at(0).remote_retention), "45");
    test_helpers::expect_eq("wizard source local retention", std::to_string(profile.sources.at(1).local_retention), "12");
    test_helpers::expect_true("wizard daily limit", !profile.settings.daily_limit, "daily limit should follow answers");
    test_helpers::expect_true("wizard keep failed snapshot", profile.settings.keep_failed_local_snapshot, "keep failed snapshot should follow answers");
    test_helpers::expect_true("wizard auto eject", !profile.settings.auto_eject, "auto eject should follow answers");
}

void test_profile_from_wizard_answers_validation() {
    test_helpers::expect_validation_error("wizard invalid profile id", [] {
        auto answers = sample_answers();
        answers.profile_id = "../bad";
        (void)btrfsbackup::profile_from_wizard_answers(answers);
    }, "profileId");

    test_helpers::expect_validation_error("wizard duplicate sources", [] {
        auto answers = sample_answers();
        answers.sources.at(1).id = "root";
        (void)btrfsbackup::profile_from_wizard_answers(answers);
    }, "duplicate source name");
}

void test_render_wizard_tree() {
    fs::path root = test_helpers::test_root("profile-wizard", "render");
    auto answers = sample_answers();
    answers.keyfile = "/root/keys/backupdisk.key";
    btrfsbackup::Profile profile = btrfsbackup::profile_from_wizard_answers(answers);

    btrfsbackup::render_wizard_tree(profile, answers.keyfile, root / "rendered");

    test_helpers::expect_true(
        "wizard render marker",
        fs::is_regular_file(root / "rendered" / btrfsbackup::render_root_marker),
        "render root marker was not created"
    );

    test_helpers::expect_contains(
        "wizard render profile",
        read_file(root / "rendered" / "config" / "profile.json"),
        "\"profileId\": \"laptop\""
    );
    test_helpers::expect_contains(
        "wizard render stored profile",
        read_file(root / "rendered" / "config" / "profiles" / "laptop" / "profile.json"),
        "\"profileId\": \"laptop\""
    );
    test_helpers::expect_contains(
        "wizard render crypttab keyfile",
        read_file(root / "rendered" / "config" / "crypttab.fragment"),
        "/root/keys/backupdisk.key"
    );
    test_helpers::expect_contains(
        "wizard render profile udev rule",
        read_file(root / "rendered" / "udev" / "99-btrfs-backup-laptop.rules"),
        "btrfs-backup@laptop.service"
    );
    test_helpers::expect_true(
        "wizard render no generic udev rule",
        !fs::exists(root / "rendered" / "udev" / "99-btrfs-backup.rules"),
        "wizard render should not duplicate the profile udev rule"
    );
    test_helpers::expect_contains(
        "wizard render mount dependency",
        read_file(root / "rendered" / "systemd" / "btrfs-backup@laptop.service.d" / "target-mount.conf"),
        "RequiresMountsFor=\"/mnt/btrfs-backup/laptop\""
    );

    test_helpers::write_file(root / "rendered" / "stale.txt", "old");
    btrfsbackup::render_wizard_tree(profile, answers.keyfile, root / "rendered");
    test_helpers::expect_true(
        "wizard rerender removes owned stale file",
        !fs::exists(root / "rendered" / "stale.txt"),
        "stale file survived rerender"
    );

    test_helpers::write_file(root / "unmarked" / "important.txt", "keep me");
    test_helpers::expect_validation_error(
        "wizard refuses unmarked directory",
        [&] { btrfsbackup::render_wizard_tree(profile, answers.keyfile, root / "unmarked"); },
        "without .btrfs-backup-render-root"
    );
    test_helpers::expect_eq(
        "wizard preserves unmarked file",
        read_file(root / "unmarked" / "important.txt"),
        "keep me"
    );

    btrfsbackup::Profile invalid = profile;
    invalid.id = "../invalid";
    test_helpers::expect_validation_error(
        "wizard validates before output access",
        [&] { btrfsbackup::render_wizard_tree(invalid, answers.keyfile, root / "unmarked"); },
        "profileId"
    );
    test_helpers::expect_eq(
        "wizard invalid profile preserves unmarked file",
        read_file(root / "unmarked" / "important.txt"),
        "keep me"
    );

    fs::remove_all(root);
}

} // namespace

int main() {
    test_prompt_parsing();
    test_prompt_defaults_and_retry();
    test_source_names();
    test_source_selection();
    test_profile_from_wizard_answers();
    test_profile_from_wizard_answers_validation();
    test_render_wizard_tree();

    return test_helpers::finish("profile wizard tests");
}
