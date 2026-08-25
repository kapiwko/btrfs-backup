// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <string>
#include <vector>

#include <backup/retention_plan.hpp>

#include "support/test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

btrfsbackup::SnapshotInfo snapshot(
    const std::string& source_id,
    const std::string& name,
    const std::string& timestamp,
    int sequence,
    const fs::path& path
) {
    return btrfsbackup::SnapshotInfo{
        .side = btrfsbackup::SnapshotSide::Local,
        .source_id = source_id,
        .name = name,
        .timestamp = timestamp,
        .sequence = sequence,
        .path = path,
        .readonly = true,
        .uuid = name,
    };
}

void test_deletes_oldest_snapshots() {
    std::vector<btrfsbackup::SnapshotInfo> snapshots{
        snapshot("home", "home-2026-08-23T080000Z", "2026-08-23T080000Z", 0, "/snap/home-3"),
        snapshot("home", "home-2026-08-21T080000Z", "2026-08-21T080000Z", 0, "/snap/home-1"),
        snapshot("root", "root-2026-08-20T080000Z", "2026-08-20T080000Z", 0, "/snap/root-1"),
        snapshot("home", "home-2026-08-22T080000Z", "2026-08-22T080000Z", 0, "/snap/home-2"),
    };

    btrfsbackup::RetentionPlan plan = btrfsbackup::plan_count_retention("home", snapshots, 2);

    test_helpers::expect_eq("delete count", std::to_string(plan.delete_snapshots.size()), "1");
    test_helpers::expect_eq("keep count", std::to_string(plan.keep.size()), "2");
    test_helpers::expect_eq("delete oldest", plan.delete_snapshots.at(0).path.string(), "/snap/home-1");
    test_helpers::expect_eq("keep middle", plan.keep.at(0).path.string(), "/snap/home-2");
    test_helpers::expect_eq("keep newest", plan.keep.at(1).path.string(), "/snap/home-3");
}

void test_sequence_ordering() {
    std::vector<btrfsbackup::SnapshotInfo> snapshots{
        snapshot("home", "home-2026-08-23T080000Z-02", "2026-08-23T080000Z", 2, "/snap/home-3"),
        snapshot("home", "home-2026-08-23T080000Z", "2026-08-23T080000Z", 0, "/snap/home-1"),
        snapshot("home", "home-2026-08-23T080000Z-01", "2026-08-23T080000Z", 1, "/snap/home-2"),
    };

    btrfsbackup::RetentionPlan plan = btrfsbackup::plan_count_retention("home", snapshots, 1);

    test_helpers::expect_eq("sequence delete count", std::to_string(plan.delete_snapshots.size()), "2");
    test_helpers::expect_eq("sequence delete base", plan.delete_snapshots.at(0).path.string(), "/snap/home-1");
    test_helpers::expect_eq("sequence delete first suffix", plan.delete_snapshots.at(1).path.string(), "/snap/home-2");
    test_helpers::expect_eq("sequence keep newest suffix", plan.keep.at(0).path.string(), "/snap/home-3");
}

void test_unlimited_and_under_limit_keep_everything() {
    std::vector<btrfsbackup::SnapshotInfo> snapshots{
        snapshot("home", "home-2026-08-21T080000Z", "2026-08-21T080000Z", 0, "/snap/home-1"),
        snapshot("home", "home-2026-08-22T080000Z", "2026-08-22T080000Z", 0, "/snap/home-2"),
    };

    btrfsbackup::RetentionPlan unlimited = btrfsbackup::plan_count_retention("home", snapshots, 0);
    test_helpers::expect_eq("unlimited deletes none", std::to_string(unlimited.delete_snapshots.size()), "0");
    test_helpers::expect_eq("unlimited keeps all", std::to_string(unlimited.keep.size()), "2");

    btrfsbackup::RetentionPlan under_limit = btrfsbackup::plan_count_retention("home", snapshots, 3);
    test_helpers::expect_eq("under limit deletes none", std::to_string(under_limit.delete_snapshots.size()), "0");
    test_helpers::expect_eq("under limit keeps all", std::to_string(under_limit.keep.size()), "2");
}

void test_invalid_keep_count() {
    test_helpers::expect_validation_error("negative retention", [] {
        (void)btrfsbackup::plan_count_retention("home", {}, -1);
    }, "retention keep count must be non-negative");
}

} // namespace

int main() {
    test_deletes_oldest_snapshots();
    test_sequence_ordering();
    test_unlimited_and_under_limit_keep_everything();
    test_invalid_keep_count();

    return test_helpers::finish("retention plan tests");
}
