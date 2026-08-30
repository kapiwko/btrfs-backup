// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <limits>
#include <string>

#include <platform/linux/storage/SpaceCheck.hpp>
#include <platform/linux/storage/FilesystemSpaceProbe.hpp>
#include <platform/linux/storage/MountInfo.hpp>

#include "support/ValidationTestHelpers.hpp"

namespace fs = std::filesystem;

namespace {

void test_available_bytes() {
    fs::path root = test_helpers::test_root("space-check", "available");
    const std::uint64_t available = btrfsbackup::platform::linux::storage::available_bytes(root);
    test_helpers::expect_true("space available", available > 0, "temporary directory should report available bytes");
    fs::remove_all(root);
}

void test_filesystem_space_math() {
    using btrfsbackup::backup::FilesystemSpace;
    test_helpers::expect_true("empty usage", FilesystemSpace{100, 100, 100}.usage_percent() == 0, "empty filesystem usage is not zero");
    test_helpers::expect_true("half usage", FilesystemSpace{100, 50, 50}.usage_percent() == 50, "half-full filesystem usage is wrong");
    test_helpers::expect_true("rounded usage", FilesystemSpace{3, 2, 2}.usage_percent() == 34, "usage was not rounded up");
    test_helpers::expect_true("full usage", FilesystemSpace{100, 0, 0}.usage_percent() == 100, "full filesystem usage is wrong");
    test_helpers::expect_true("invalid free", !FilesystemSpace{100, 101, 0}.valid(), "free space above capacity was accepted");
    test_helpers::expect_true("invalid available", !FilesystemSpace{100, 50, 51}.valid(), "available space above free space was accepted");
    test_helpers::expect_true(
        "large usage",
        FilesystemSpace{std::numeric_limits<std::uint64_t>::max(), 1, 1}.usage_percent() == 100,
        "large filesystem usage overflowed"
    );
}

void test_verified_mount_probe() {
    fs::path root = test_helpers::test_root("space-check", "verified-mount");
    const auto mounts = btrfsbackup::platform::linux::storage::read_mount_table();
    const auto mount = btrfsbackup::backup::mount_for_path(mounts, root);
    test_helpers::expect_true("test mount found", mount.has_value(), "temporary directory mount was not found");
    if (!mount.has_value()) {
        fs::remove_all(root);
        return;
    }

    const btrfsbackup::platform::linux::storage::FilesystemSpaceProbe probe;
    const auto space = probe.measure_verified_mount(root, *mount);
    test_helpers::expect_true("verified capacity", space.capacity_bytes > 0, "verified mount has no capacity");

    auto wrong_mount = *mount;
    ++wrong_mount.mount_id;
    test_helpers::expect_validation_error(
        "mount identity mismatch",
        [&] { (void)probe.measure_verified_mount(root, wrong_mount); },
        "identity changed"
    );
    fs::remove_all(root);
}

void test_minimum_free_space() {
    fs::path root = test_helpers::test_root("space-check", "minimum");
    btrfsbackup::platform::linux::storage::check_minimum_free_space(root, 0, "test");
    btrfsbackup::platform::linux::storage::check_minimum_free_space(root, 1, "test");
    test_helpers::expect_validation_error("space too low", [&] { btrfsbackup::platform::linux::storage::check_minimum_free_space(root, std::numeric_limits<std::uint64_t>::max(), "test"); }, "Insufficient free space");
    fs::remove_all(root);
}

void test_missing_path() {
    test_helpers::expect_validation_error("space missing", [] { (void)btrfsbackup::platform::linux::storage::available_bytes("/tmp/does-not-exist-btrfs-backup-space-check"); }, "Could not determine free space");
}

} // namespace

int main() {
    test_available_bytes();
    test_filesystem_space_math();
    test_verified_mount_probe();
    test_minimum_free_space();
    test_missing_path();

    return test_helpers::finish("space check tests");
}
