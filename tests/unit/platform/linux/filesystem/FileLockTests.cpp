// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <type_traits>

#include <platform/linux/filesystem/FileLock.hpp>

#include "support/ValidationTestHelpers.hpp"

namespace fs = std::filesystem;

namespace {

static_assert(std::is_nothrow_destructible_v<btrfsbackup::platform::linux::filesystem::FileLock>);
static_assert(std::is_nothrow_move_constructible_v<btrfsbackup::platform::linux::filesystem::FileLock>);
static_assert(std::is_nothrow_move_assignable_v<btrfsbackup::platform::linux::filesystem::FileLock>);

void test_lock_lifecycle() {
    fs::path root = test_helpers::test_root("file-lock", "lifecycle");
    fs::path lock_path = root / "run" / "backup.lock";

    btrfsbackup::platform::linux::filesystem::FileLock first(lock_path);
    test_helpers::expect_true("first lock", first.try_acquire(), "first lock should be acquired");
    test_helpers::expect_true("first acquired", first.acquired(), "first lock state should be acquired");
    test_helpers::expect_true("lock file exists", fs::is_regular_file(lock_path), "lock file should exist");

    btrfsbackup::platform::linux::filesystem::FileLock second(lock_path);
    test_helpers::expect_true("second blocked", !second.try_acquire(), "second lock should be blocked");
    test_helpers::expect_true("second not acquired", !second.acquired(), "second lock state should not be acquired");

    first.release();
    test_helpers::expect_true("second acquired after release", second.try_acquire(), "second lock should acquire after release");

    fs::remove_all(root);
}

void test_lock_released_by_destructor() {
    fs::path root = test_helpers::test_root("file-lock", "destructor");
    fs::path lock_path = root / "backup.lock";
    {
        btrfsbackup::platform::linux::filesystem::FileLock first(lock_path);
        test_helpers::expect_true("destructor first lock", first.try_acquire(), "first lock should be acquired");
    }
    btrfsbackup::platform::linux::filesystem::FileLock second(lock_path);
    test_helpers::expect_true("destructor released lock", second.try_acquire(), "lock should be released by destructor");
    fs::remove_all(root);
}

void test_shared_locks_block_exclusive_until_last_release() {
    fs::path root = test_helpers::test_root("file-lock", "shared");
    fs::path lock_path = root / "target.lock";
    btrfsbackup::platform::linux::filesystem::FileLock first(lock_path);
    btrfsbackup::platform::linux::filesystem::FileLock second(lock_path);
    btrfsbackup::platform::linux::filesystem::FileLock exclusive(lock_path);
    test_helpers::expect_true("first shared", first.try_acquire_shared(), "first shared lock failed");
    test_helpers::expect_true("second shared", second.try_acquire_shared(), "second shared lock failed");
    test_helpers::expect_true("exclusive blocked", !exclusive.try_acquire(), "exclusive lock bypassed browse leases");
    test_helpers::expect_true("upgrade blocked", !first.try_upgrade_to_exclusive(), "shared lock upgraded while another user remained");
    second.release();
    test_helpers::expect_true("upgrade after last user", first.try_upgrade_to_exclusive(), "last shared lock did not become exclusive");
    first.downgrade_to_shared();
    first.release();
    test_helpers::expect_true("exclusive after release", exclusive.try_acquire(), "target remained locked after last user");
    fs::remove_all(root);
}

void test_lock_paths_use_separate_profile_and_target_namespaces() {
    fs::path root = test_helpers::test_root("file-lock", "paths");
    test_helpers::expect_eq(
        "profile lock path",
        btrfsbackup::platform::linux::filesystem::profile_lock_path(root, btrfsbackup::ProfileId{"default"}).string(),
        (root / "profiles" / "default.lock").string()
    );
    test_helpers::expect_eq(
        "target lock path",
        btrfsbackup::platform::linux::filesystem::target_lock_path(root, btrfsbackup::config::LuksUuid{"AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE"}).string(),
        (root / "targets" / "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee.lock").string()
    );
}

void test_lock_rejects_symbolic_link() {
    fs::path root = test_helpers::test_root("file-lock", "symlink");
    fs::create_directories(root / "run");
    test_helpers::write_file(root / "unexpected", "not a lock\n");
    fs::create_symlink(root / "unexpected", root / "run" / "backup.lock");

    btrfsbackup::platform::linux::filesystem::FileLock lock(root / "run" / "backup.lock");
    test_helpers::expect_validation_error(
        "lock symlink",
        [&] { (void)lock.try_acquire(); },
        "cannot open lock file"
    );
    fs::remove_all(root);
}

} // namespace

int main() {
    test_lock_lifecycle();
    test_lock_released_by_destructor();
    test_shared_locks_block_exclusive_until_last_release();
    test_lock_paths_use_separate_profile_and_target_namespaces();
    test_lock_rejects_symbolic_link();

    return test_helpers::finish("file lock tests");
}
