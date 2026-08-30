// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <filesystem>
#include <string>

#include <platform/linux/filesystem/InotifyFileChangeWatcher.hpp>

#include "support/ValidationTestHelpers.hpp"

namespace fs = std::filesystem;

namespace {

using btrfsbackup::platform::linux::filesystem::InotifyFileChangeWatcher;

void test_wakes_after_atomic_replacement() {
    const fs::path root = test_helpers::test_root("inotify-file-change-watcher", "replace");
    const fs::path target = root / "current.json";
    const fs::path temporary = root / ".current.json.new";
    test_helpers::write_file(target, "old");
    InotifyFileChangeWatcher watcher(target);

    test_helpers::write_file(temporary, "new");
    fs::rename(temporary, target);

    watcher.wait_for_change(std::chrono::seconds{1});
    test_helpers::expect_eq(
        "atomic replacement",
        std::to_string(fs::file_size(target)),
        "3"
    );
    fs::remove_all(root);
}

void test_watches_nearest_parent_until_profile_exists() {
    const fs::path root = test_helpers::test_root("inotify-file-change-watcher", "bootstrap");
    const fs::path target = root / "default" / "current.json";
    InotifyFileChangeWatcher watcher(target);

    test_helpers::write_file(target, "status");

    watcher.wait_for_change(std::chrono::seconds{1});
    test_helpers::expect_true("bootstrap target", fs::is_regular_file(target), "target was not created");
    fs::remove_all(root);
}

void test_timeout_allows_explicit_resynchronization() {
    const fs::path root = test_helpers::test_root("inotify-file-change-watcher", "timeout");
    InotifyFileChangeWatcher watcher(root / "current.json");
    const auto started = std::chrono::steady_clock::now();

    watcher.wait_for_change(std::chrono::milliseconds{10});

    test_helpers::expect_true(
        "resync timeout",
        std::chrono::steady_clock::now() - started >= std::chrono::milliseconds{5},
        "watch returned before the timeout"
    );
    fs::remove_all(root);
}

void test_rearms_after_watched_directory_is_recreated() {
    const fs::path root = test_helpers::test_root("inotify-file-change-watcher", "recreate");
    const fs::path profile = root / "default";
    const fs::path target = profile / "current.json";
    test_helpers::write_file(target, "first");
    InotifyFileChangeWatcher watcher(target);

    fs::remove_all(profile);
    test_helpers::write_file(target, "second");
    watcher.wait_for_change(std::chrono::seconds{1});
    test_helpers::write_file(target, "third");
    watcher.wait_for_change(std::chrono::seconds{1});

    test_helpers::expect_eq("rearmed watch", std::to_string(fs::file_size(target)), "5");
    fs::remove_all(root);
}

} // namespace

int main() {
    test_wakes_after_atomic_replacement();
    test_watches_nearest_parent_until_profile_exists();
    test_timeout_allows_explicit_resynchronization();
    test_rearms_after_watched_directory_is_recreated();
    return test_helpers::finish("inotify file change watcher tests");
}
