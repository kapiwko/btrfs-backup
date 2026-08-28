// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <limits>
#include <string>

#include <platform/linux/space_check.hpp>

#include "support/validation_test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

void test_available_bytes() {
    fs::path root = test_helpers::test_root("space-check", "available");
    const std::uint64_t available = btrfsbackup::platform::linux::available_bytes(root);
    test_helpers::expect_true("space available", available > 0, "temporary directory should report available bytes");
    fs::remove_all(root);
}

void test_minimum_free_space() {
    fs::path root = test_helpers::test_root("space-check", "minimum");
    btrfsbackup::platform::linux::check_minimum_free_space(root, 0, "test");
    btrfsbackup::platform::linux::check_minimum_free_space(root, 1, "test");
    test_helpers::expect_validation_error("space too low", [&] { btrfsbackup::platform::linux::check_minimum_free_space(root, std::numeric_limits<std::uint64_t>::max(), "test"); }, "Insufficient free space");
    fs::remove_all(root);
}

void test_missing_path() {
    test_helpers::expect_validation_error("space missing", [] { (void)btrfsbackup::platform::linux::available_bytes("/tmp/does-not-exist-btrfs-backup-space-check"); }, "Could not determine free space");
}

} // namespace

int main() {
    test_available_bytes();
    test_minimum_free_space();
    test_missing_path();

    return test_helpers::finish("space check tests");
}
