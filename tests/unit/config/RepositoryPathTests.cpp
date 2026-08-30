// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <string>
#include <type_traits>

#include <config/domain/RepositoryPath.hpp>

#include "support/TestHelpers.hpp"
#include "support/ValidationTestHelpers.hpp"

namespace {

static_assert(!std::is_default_constructible_v<btrfsbackup::config::RemoteSnapshotRoot>);
static_assert(!std::is_default_constructible_v<btrfsbackup::config::IncomingRoot>);
static_assert(!std::is_default_constructible_v<btrfsbackup::config::SafeRelativePath>);
static_assert(!std::is_convertible_v<std::string, btrfsbackup::config::RemoteSnapshotRoot>);
static_assert(!std::is_convertible_v<btrfsbackup::config::RemoteSnapshotRoot, btrfsbackup::config::IncomingRoot>);

void test_repository_roots_require_absolute_paths() {
    const btrfsbackup::config::RemoteSnapshotRoot remote{"/mnt/backup/../backup/snapshots"};
    const btrfsbackup::config::IncomingRoot incoming{"/mnt/backup/.incoming"};

    test_helpers::expect_eq("normalized remote root", remote.value().string(), "/mnt/backup/snapshots");
    test_helpers::expect_eq("incoming root", incoming.value().string(), "/mnt/backup/.incoming");
    test_helpers::expect_validation_error("relative remote root", [] { (void)btrfsbackup::config::RemoteSnapshotRoot{"snapshots"}; }, "absolute path");
    test_helpers::expect_validation_error("relative incoming root", [] { (void)btrfsbackup::config::IncomingRoot{".incoming"}; }, "absolute path");
}

void test_safe_relative_path_accepts_canonical_segments() {
    const btrfsbackup::config::SafeRelativePath path{"snapshots/home"};
    test_helpers::expect_eq("safe relative path", path.value().string(), "snapshots/home");
}

void test_safe_relative_path_rejects_unsafe_or_noncanonical_values() {
    test_helpers::expect_validation_error("empty relative path", [] { (void)btrfsbackup::config::SafeRelativePath{""}; }, "non-empty relative path");
    test_helpers::expect_validation_error("absolute path", [] { (void)btrfsbackup::config::SafeRelativePath{"/snapshots/home"}; }, "safe relative path");
    test_helpers::expect_validation_error("parent segment", [] { (void)btrfsbackup::config::SafeRelativePath{"snapshots/../home"}; }, "safe relative path");
    test_helpers::expect_validation_error("current segment", [] { (void)btrfsbackup::config::SafeRelativePath{"snapshots/./home"}; }, "safe relative path");
    test_helpers::expect_validation_error("empty segment", [] { (void)btrfsbackup::config::SafeRelativePath{"snapshots//home"}; }, "no empty segments");
    test_helpers::expect_validation_error("trailing empty segment", [] { (void)btrfsbackup::config::SafeRelativePath{"snapshots/home/"}; }, "safe relative path");
}

} // namespace

int main() {
    test_repository_roots_require_absolute_paths();
    test_safe_relative_path_accepts_canonical_segments();
    test_safe_relative_path_rejects_unsafe_or_noncanonical_values();
    return test_helpers::finish("repository path tests");
}
