#include <filesystem>

#include <btrfsbackup/file_lock.hpp>

#include "test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

void test_lock_lifecycle() {
    fs::path root = test_helpers::test_root("file-lock", "lifecycle");
    fs::path lock_path = root / "run" / "backup.lock";

    btrfsbackup::FileLock first(lock_path);
    test_helpers::expect_true("first lock", first.try_acquire(), "first lock should be acquired");
    test_helpers::expect_true("first acquired", first.acquired(), "first lock state should be acquired");
    test_helpers::expect_true("lock file exists", fs::is_regular_file(lock_path), "lock file should exist");

    btrfsbackup::FileLock second(lock_path);
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
        btrfsbackup::FileLock first(lock_path);
        test_helpers::expect_true("destructor first lock", first.try_acquire(), "first lock should be acquired");
    }
    btrfsbackup::FileLock second(lock_path);
    test_helpers::expect_true("destructor released lock", second.try_acquire(), "lock should be released by destructor");
    fs::remove_all(root);
}

void test_lock_paths_use_separate_profile_and_target_namespaces() {
    fs::path root = test_helpers::test_root("file-lock", "paths");
    test_helpers::expect_eq(
        "profile lock path",
        btrfsbackup::profile_lock_path(root, "default").string(),
        (root / "profiles" / "default.lock").string()
    );
    test_helpers::expect_eq(
        "target lock path",
        btrfsbackup::target_lock_path(root, "AAAAAAAA-BBBB-CCCC-DDDD-EEEEEEEEEEEE").string(),
        (root / "targets" / "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee.lock").string()
    );
}

void test_lock_rejects_symbolic_link() {
    fs::path root = test_helpers::test_root("file-lock", "symlink");
    fs::create_directories(root / "run");
    test_helpers::write_file(root / "unexpected", "not a lock\n");
    fs::create_symlink(root / "unexpected", root / "run" / "backup.lock");

    btrfsbackup::FileLock lock(root / "run" / "backup.lock");
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
    test_lock_paths_use_separate_profile_and_target_namespaces();
    test_lock_rejects_symbolic_link();

    return test_helpers::finish("file lock tests");
}
