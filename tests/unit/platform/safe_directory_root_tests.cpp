// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <fstream>

#include <core/errors.hpp>
#include <platform/linux/safe_directory_root.hpp>

#include "support/validation_test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

void test_removes_plain_tree_without_following_links() {
    fs::path test_root = test_helpers::test_root("safe-directory-root", "remove");
    fs::path root = test_root / "repository";
    fs::path outside = test_root / "outside";
    fs::create_directories(root / ".incoming" / "plain" / "nested");
    fs::create_directories(outside);
    std::ofstream(root / ".incoming" / "plain" / "nested" / "payload") << "remove";
    std::ofstream(outside / "sentinel") << "keep";

    btrfsbackup::SafeDirectoryRoot safe(root);
    safe.remove_tree(root / ".incoming" / "plain");
    test_helpers::expect_true("plain tree removed", !fs::exists(root / ".incoming" / "plain"), "plain tree survived");

    fs::create_directory_symlink(outside, root / ".incoming" / "escape");
    test_helpers::expect_validation_error("symlink cleanup rejected", [&] {
        safe.remove_contents(root / ".incoming");
    }, "symbolic link is forbidden");
    test_helpers::expect_true("outside sentinel preserved", fs::is_regular_file(outside / "sentinel"), "cleanup followed a symlink outside the repository");

    fs::remove_all(test_root);
}

void test_rejects_symlinks_in_root_and_directory_components() {
    fs::path test_root = test_helpers::test_root("safe-directory-root", "components");
    fs::path real_root = test_root / "real";
    fs::create_directories(real_root);
    fs::create_directory_symlink(real_root, test_root / "root-link");

    test_helpers::expect_validation_error("symlink root rejected", [&] {
        btrfsbackup::SafeDirectoryRoot unsafe(test_root / "root-link");
    }, "Too many levels of symbolic links");

    btrfsbackup::SafeDirectoryRoot safe(real_root);
    fs::create_directories(test_root / "outside");
    fs::create_directory_symlink(test_root / "outside", real_root / "incoming");
    test_helpers::expect_validation_error("symlink mkdir rejected", [&] {
        safe.ensure_directory(real_root / "incoming" / "run");
    }, "Too many levels of symbolic links");
    test_helpers::expect_true("outside run absent", !fs::exists(test_root / "outside" / "run"), "mkdir followed a symlink outside the repository");

    fs::remove_all(test_root);
}

void test_rejects_lexical_escape() {
    fs::path test_root = test_helpers::test_root("safe-directory-root", "escape");
    fs::path root = test_root / "repository";
    fs::create_directories(root);
    btrfsbackup::SafeDirectoryRoot safe(root);

    test_helpers::expect_validation_error("lexical escape rejected", [&] {
        (void)safe.exists(root / ".." / "outside");
    }, "path escapes safe directory root");
    test_helpers::expect_validation_error("root removal rejected", [&] {
        safe.remove_tree(root);
    }, "refusing to remove safe directory root");

    fs::remove_all(test_root);
}

} // namespace

int main() {
    test_removes_plain_tree_without_following_links();
    test_rejects_symlinks_in_root_and_directory_components();
    test_rejects_lexical_escape();
    return test_helpers::finish("safe directory root tests");
}
