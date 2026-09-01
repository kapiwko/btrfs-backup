// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <sys/stat.h>
#include <unistd.h>

#include <filesystem>
#include <stdexcept>

#include <platform/linux/filesystem/TrustedDirectory.hpp>

#include "support/ValidationTestHelpers.hpp"

namespace fs = std::filesystem;

namespace {

mode_t permissions(const fs::path& path) {
    struct stat status{};
    if (stat(path.c_str(), &status) != 0) {
        throw std::runtime_error("cannot inspect test path");
    }
    return status.st_mode & 0777;
}

void test_creates_directory_through_trusted_parents() {
    fs::path root = test_helpers::test_root("trusted-directory", "create");
    chmod(root.c_str(), 0755);

    fs::path mount_point = root / "mounts" / "default";
    btrfsbackup::platform::linux::filesystem::ensure_trusted_directory(mount_point, 0755, root, geteuid());

    test_helpers::expect_true("trusted mount created", fs::is_directory(mount_point), "mount point was not created");
    test_helpers::expect_true("trusted mount mode", permissions(mount_point) == 0755, "mount point mode is not 0755");
    fs::remove_all(root);
}

void test_rejects_symlink_without_changing_target_permissions() {
    fs::path root = test_helpers::test_root("trusted-directory", "symlink");
    chmod(root.c_str(), 0755);
    fs::path mount_root = root / "mounts";
    fs::path victim = root / "victim";
    fs::create_directories(mount_root);
    fs::create_directories(victim);
    chmod(victim.c_str(), 0700);
    fs::create_directory_symlink(victim, mount_root / "default");

    test_helpers::expect_validation_error("mount symlink rejected", [&] { btrfsbackup::platform::linux::filesystem::ensure_trusted_directory(mount_root / "default", 0755, root, geteuid()); }, "without symlinks");
    test_helpers::expect_true("victim mode unchanged", permissions(victim) == 0700, "symlink target permissions changed");
    fs::remove_all(root);
}

void test_rejects_writable_parent() {
    fs::path root = test_helpers::test_root("trusted-directory", "writable-parent");
    chmod(root.c_str(), 0755);
    fs::path mount_root = root / "mounts";
    fs::create_directories(mount_root);
    chmod(mount_root.c_str(), 0777);

    test_helpers::expect_validation_error("writable mount parent rejected", [&] { btrfsbackup::platform::linux::filesystem::ensure_trusted_directory(mount_root / "default", 0755, root, geteuid()); }, "writable by group or others");
    test_helpers::expect_true("mount not created", !fs::exists(mount_root / "default"), "created below writable parent");
    fs::remove_all(root);
}

void test_safe_filename_rejects_paths_and_special_components() {
    using btrfsbackup::platform::linux::filesystem::SafeFilename;
    test_helpers::expect_validation_error("absolute filename rejected", [] { static_cast<void>(SafeFilename("/tmp/key")); }, "unsafe filename");
    test_helpers::expect_validation_error("nested filename rejected", [] { static_cast<void>(SafeFilename("nested/key")); }, "unsafe filename");
    test_helpers::expect_validation_error("parent filename rejected", [] { static_cast<void>(SafeFilename("..")); }, "unsafe filename");
}

} // namespace

int main() {
    test_creates_directory_through_trusted_parents();
    test_rejects_symlink_without_changing_target_permissions();
    test_rejects_writable_parent();
    test_safe_filename_rejects_paths_and_special_components();
    return test_helpers::finish("trusted directory tests passed");
}
