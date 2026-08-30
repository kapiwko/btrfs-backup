// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <cstddef>
#include <string>
#include <utility>

#include <backup/BackupPlanBuilder.hpp>

#include "support/TestHelpers.hpp"

namespace {

btrfsbackup::config::Profile profile() {
    btrfsbackup::config::Profile result{
        btrfsbackup::ProfileId{"default"},
        {
            btrfsbackup::config::LuksUuid{"11111111-2222-3333-4444-555555555555"},
            btrfsbackup::config::BtrfsUuid{"22222222-3333-4444-5555-666666666666"},
            btrfsbackup::config::PartitionUuid{""},
            btrfsbackup::config::MapperName{"backup"},
        },
        {
            btrfsbackup::config::RemoteSnapshotRoot{"/mnt/backup/default/snapshots"},
            btrfsbackup::config::IncomingRoot{"/mnt/backup/default/.incoming"},
        },
    };
    result.target.mount_point = btrfsbackup::config::TargetMountPoint{"/mnt/backup"};
    result.settings.incremental_required = false;
    btrfsbackup::config::ProfileSource source{btrfsbackup::SourceId{"root"}};
    source.subvolume = btrfsbackup::config::SourceSubvolumePath{"/"};
    source.local_snapshot_dir = btrfsbackup::config::LocalSnapshotRoot{"/.snapshots/root"};
    source.local_retention = btrfsbackup::config::RetentionCount{2};
    source.remote_retention = btrfsbackup::config::RetentionCount{2};
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
    btrfsbackup::CancellationToken cancellation;

    const btrfsbackup::backup::BackupRunPlan first = builder.build(
        profile(),
        snapshot,
        run_id,
        "2026-08-23T080000Z",
        cancellation
    );
    const btrfsbackup::backup::BackupRunPlan second = builder.build(
        profile(),
        snapshot,
        run_id,
        "2026-08-23T080000Z",
        cancellation
    );
    const auto& first_snapshot = std::get<btrfsbackup::backup::CreateSnapshotAction>(first.sources.at(0).actions().at(1));
    const auto& second_snapshot = std::get<btrfsbackup::backup::CreateSnapshotAction>(second.sources.at(0).actions().at(1));

    test_helpers::expect_eq(
        "deterministic local snapshot",
        first_snapshot.snapshot.string(),
        second_snapshot.snapshot.string()
    );
    test_helpers::expect_eq(
        "deterministic action count",
        std::to_string(first.sources.at(0).actions().size()),
        std::to_string(second.sources.at(0).actions().size())
    );
    for (std::size_t index = 0; index < first.sources.at(0).actions().size(); ++index) {
        test_helpers::expect_true(
            "deterministic action " + std::to_string(index),
            btrfsbackup::backup::backup_run_action_kind(first.sources.at(0).actions().at(index)) == btrfsbackup::backup::backup_run_action_kind(second.sources.at(0).actions().at(index)),
            "action kinds differ"
        );
    }
}

} // namespace

int main() {
    test_same_snapshot_builds_same_plan();
    return test_helpers::finish("backup plan builder tests");
}
