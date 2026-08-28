// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstddef>
#include <string>
#include <utility>

#include <backup/backup_plan_builder.hpp>

#include "support/test_helpers.hpp"

namespace {

btrfsbackup::config::Profile profile() {
    btrfsbackup::config::Profile result{btrfsbackup::ProfileId{"default"}};
    result.target.mount_point = "/mnt/backup";
    result.paths.remote_root = "/mnt/backup/default/snapshots";
    result.paths.incoming_root = "/mnt/backup/default/.incoming";
    result.settings.incremental_required = false;
    btrfsbackup::config::ProfileSource source{btrfsbackup::SourceId{"root"}};
    source.subvolume = "/";
    source.local_snapshot_dir = "/.snapshots/root";
    source.remote_subdir = "root";
    source.local_retention = 2;
    source.remote_retention = 2;
    result.sources.push_back(std::move(source));
    return result;
}

void test_same_snapshot_builds_same_plan() {
    const btrfsbackup::backup::BackupPlanningSnapshot snapshot{
        {},
        {},
        {},
        {},
        "/var/lib/btrfs-backup/profiles/default",
    };
    const btrfsbackup::backup::BackupPlanBuilder builder;
    const btrfsbackup::RunId run_id{"20260823T080000Z-123-456"};

    const btrfsbackup::backup::BackupRunPlan first = builder.build(
        profile(),
        snapshot,
        run_id,
        "2026-08-23T080000Z"
    );
    const btrfsbackup::backup::BackupRunPlan second = builder.build(
        profile(),
        snapshot,
        run_id,
        "2026-08-23T080000Z"
    );

    test_helpers::expect_eq(
        "deterministic local snapshot",
        first.sources.at(0).local_snapshot_path.string(),
        second.sources.at(0).local_snapshot_path.string()
    );
    test_helpers::expect_eq(
        "deterministic action count",
        std::to_string(first.sources.at(0).actions.size()),
        std::to_string(second.sources.at(0).actions.size())
    );
    for (std::size_t index = 0; index < first.sources.at(0).actions.size(); ++index) {
        test_helpers::expect_true(
            "deterministic action " + std::to_string(index),
            btrfsbackup::backup::backup_run_action_kind(first.sources.at(0).actions.at(index)) == btrfsbackup::backup::backup_run_action_kind(second.sources.at(0).actions.at(index)),
            "action kinds differ"
        );
    }
}

} // namespace

int main() {
    test_same_snapshot_builds_same_plan();
    return test_helpers::finish("backup plan builder tests");
}
