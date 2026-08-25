// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>

#include <platform/linux/trusted_file.hpp>

#include "support/test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

void test_rootless_policy_accepts_current_user_private_file() {
    fs::path root = test_helpers::test_root("trusted-file", "rootless");
    fs::path config = root / "profile.json";
    test_helpers::write_file(config, "{}\n");
    chmod(config.c_str(), 0600);

    btrfsbackup::assert_trusted_config_file(config, {.allow_current_user_owner = true});
    test_helpers::expect_eq(
        "trusted read content",
        btrfsbackup::read_trusted_config_file(config, {.allow_current_user_owner = true}),
        "{}\n"
    );

    fs::remove_all(root);
}

void test_default_policy_rejects_current_user_file_when_not_root() {
    if (geteuid() == 0) {
        return;
    }
    fs::path root = test_helpers::test_root("trusted-file", "owner");
    fs::path config = root / "profile.json";
    test_helpers::write_file(config, "{}\n");
    chmod(config.c_str(), 0600);

    test_helpers::expect_validation_error("trusted owner", [&] {
        btrfsbackup::assert_trusted_config_file(config);
    }, "owned by root");

    fs::remove_all(root);
}

void test_rejects_public_permissions() {
    fs::path root = test_helpers::test_root("trusted-file", "mode");
    fs::path config = root / "profile.json";
    test_helpers::write_file(config, "{}\n");
    chmod(config.c_str(), 0640);

    test_helpers::expect_validation_error("trusted mode", [&] {
        btrfsbackup::assert_trusted_config_file(config, {.allow_current_user_owner = true});
    }, "group or others");

    fs::remove_all(root);
}

void test_rejects_missing_or_directory() {
    fs::path root = test_helpers::test_root("trusted-file", "missing");

    test_helpers::expect_validation_error("trusted missing", [&] {
        btrfsbackup::assert_trusted_config_file(root / "missing.json", {.allow_current_user_owner = true});
    }, "not a regular file");
    test_helpers::expect_validation_error("trusted directory", [&] {
        btrfsbackup::assert_trusted_config_file(root, {.allow_current_user_owner = true});
    }, "not a regular file");

    fs::remove_all(root);
}

void test_rejects_symbolic_link() {
    fs::path root = test_helpers::test_root("trusted-file", "symlink");
    fs::path real_config = root / "real-profile.json";
    fs::path config = root / "profile.json";
    test_helpers::write_file(real_config, "{}\n");
    chmod(real_config.c_str(), 0600);
    fs::create_symlink(real_config, config);

    test_helpers::expect_validation_error("trusted symlink", [&] {
        (void)btrfsbackup::read_trusted_config_file(config, {.allow_current_user_owner = true});
    }, "not a regular file");

    fs::remove_all(root);
}

} // namespace

int main() {
    test_rootless_policy_accepts_current_user_private_file();
    test_default_policy_rejects_current_user_file_when_not_root();
    test_rejects_public_permissions();
    test_rejects_missing_or_directory();
    test_rejects_symbolic_link();

    return test_helpers::finish("trusted file tests");
}
