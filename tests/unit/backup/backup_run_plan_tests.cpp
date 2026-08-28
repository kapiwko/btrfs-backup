// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <backup/model/backup_run_plan.hpp>

#include "support/validation_test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

static_assert(!std::is_default_constructible_v<btrfsbackup::backup::CreateSnapshotAction>);
static_assert(!std::is_default_constructible_v<btrfsbackup::backup::BackupRunAction>);

btrfsbackup::config::Profile profile() {
    btrfsbackup::config::Profile result{btrfsbackup::ProfileId{"default"}};
    result.name = "Default backup";
    result.target.mapper_name = "backup";
    result.target.mount_point = "/mnt/backup";
    result.target.btrfs_uuid = "target-fs";
    result.paths.remote_root = "/mnt/backup/snapshots";
    result.paths.incoming_root = "/mnt/backup/.incoming";
    result.settings.incremental_required = true;
    result.settings.keep_failed_local_snapshot = false;
    btrfsbackup::config::ProfileSource root{btrfsbackup::SourceId{"root"}};
    root.name = "System";
    root.subvolume = "/";
    root.local_snapshot_dir = "/.snapshots/root";
    root.remote_subdir = "root";
    root.remote_retention = 2;
    root.local_retention = 2;
    btrfsbackup::config::ProfileSource home{btrfsbackup::SourceId{"home"}};
    home.name = "Home";
    home.enabled = false;
    home.subvolume = "/home";
    home.local_snapshot_dir = "/.snapshots/home";
    home.remote_subdir = "home";
    home.remote_retention = 2;
    home.local_retention = 2;
    result.sources = {std::move(root), std::move(home)};
    return result;
}

btrfsbackup::backup::SnapshotInfo snapshot(
    btrfsbackup::backup::SnapshotSide side,
    const std::string& source_id,
    const std::string& name,
    const std::string& timestamp,
    const fs::path& path,
    const std::string& uuid,
    const std::string& received_uuid = ""
) {
    return btrfsbackup::backup::SnapshotInfo{
        .side = side,
        .source_id = btrfsbackup::SourceId{source_id},
        .name = name,
        .timestamp = timestamp,
        .sequence = 0,
        .path = path,
        .readonly = true,
        .uuid = uuid,
        .received_uuid = received_uuid,
    };
}

void test_builds_ordered_source_plan() {
    btrfsbackup::backup::SnapshotInventoryBySource local{
        {
            btrfsbackup::SourceId{"root"},
            {
                snapshot(
                    btrfsbackup::backup::SnapshotSide::Local,
                    "root",
                    "root-2026-08-22T080000Z",
                    "2026-08-22T080000Z",
                    "/.snapshots/root/root-2026-08-22T080000Z",
                    "parent-uuid"
                ),
            },
        },
    };
    btrfsbackup::backup::SnapshotInventoryBySource remote{
        {
            btrfsbackup::SourceId{"root"},
            {
                snapshot(
                    btrfsbackup::backup::SnapshotSide::Remote,
                    "root",
                    "root-2026-08-22T080000Z",
                    "2026-08-22T080000Z",
                    "/mnt/backup/snapshots/root/root-2026-08-22T080000Z",
                    "remote-uuid",
                    "parent-uuid"
                ),
            },
        },
    };

    btrfsbackup::backup::BackupRunPlan plan = btrfsbackup::backup::build_backup_run_plan(
        profile(),
        local,
        remote,
        {},
        {},
        "/var/lib/btrfs-backup/profiles/default",
        btrfsbackup::RunId{"20260823T080000Z-123-456"},
        "2026-08-23T080000Z"
    );

    test_helpers::expect_eq("plan source count", std::to_string(plan.sources.size()), "1");
    const btrfsbackup::backup::BackupSourceRunPlan& source = plan.sources.at(0);
    test_helpers::expect_eq("plan source id", std::string(source.source_id.value()), "root");
    test_helpers::expect_eq("plan snapshot path", source.local_snapshot_path.string(), "/.snapshots/root/root-2026-08-23T080000Z");
    test_helpers::expect_true("plan incremental", source.parent.incremental, "expected incremental parent");
    test_helpers::expect_eq("plan parent", source.parent.local_parent->path.string(), "/.snapshots/root/root-2026-08-22T080000Z");
    test_helpers::expect_eq("plan actions", std::to_string(source.actions.size()), "8");
    test_helpers::expect_eq("first action", std::to_string(static_cast<int>(btrfsbackup::backup::backup_run_action_kind(source.actions.at(0)))), std::to_string(static_cast<int>(btrfsbackup::backup::BackupRunActionKind::CleanupIncoming)));
    test_helpers::expect_eq("last action", std::to_string(static_cast<int>(btrfsbackup::backup::backup_run_action_kind(source.actions.back()))), std::to_string(static_cast<int>(btrfsbackup::backup::BackupRunActionKind::CleanupSource)));

    const auto& create_snapshot = std::get<btrfsbackup::backup::CreateSnapshotAction>(source.actions.at(1));
    test_helpers::expect_eq("create source", create_snapshot.source.string(), "/");
    test_helpers::expect_eq("create snapshot", create_snapshot.snapshot.string(), "/.snapshots/root/root-2026-08-23T080000Z");
    test_helpers::expect_eq("create run id", std::string(create_snapshot.run_id.value()), "20260823T080000Z-123-456");

    const auto& send_receive = std::get<btrfsbackup::backup::SendReceiveAction>(source.actions.at(2));
    test_helpers::expect_eq("send snapshot", send_receive.snapshot.string(), create_snapshot.snapshot.string());
    test_helpers::expect_eq("send parent", send_receive.parent->string(), "/.snapshots/root/root-2026-08-22T080000Z");
    test_helpers::expect_eq("receive directory", send_receive.incoming_run_directory.string(), "/mnt/backup/.incoming/root/20260823T080000Z-123-456");
}

void test_inserts_snapshot_hooks_around_snapshot_creation() {
    btrfsbackup::config::Profile test_profile = profile();
    test_profile.settings.incremental_required = false;
    test_profile.hooks.before_snapshot = {
        btrfsbackup::config::ProfileHookCommand{
            .program = "/etc/btrfs-backup/hooks.d/before",
            .arguments = {"root"},
            .timeout = std::chrono::seconds{30},
        },
    };
    test_profile.hooks.after_snapshot = {
        btrfsbackup::config::ProfileHookCommand{
            .program = "/etc/btrfs-backup/hooks.d/after",
            .arguments = {"root"},
            .timeout = std::chrono::seconds{60},
        },
    };

    btrfsbackup::backup::BackupRunPlan plan = btrfsbackup::backup::build_backup_run_plan(
        test_profile,
        {},
        {},
        {},
        {},
        "/var/lib/btrfs-backup/profiles/default",
        btrfsbackup::RunId{"20260823T080000Z-123-456"},
        "2026-08-23T080000Z"
    );

    const std::vector<btrfsbackup::backup::BackupRunAction>& actions = plan.sources.at(0).actions;
    const auto& before_hook = std::get<btrfsbackup::backup::RunHookAction>(actions.at(1));
    const auto& after_hook = std::get<btrfsbackup::backup::RunHookAction>(actions.at(3));
    test_helpers::expect_eq("hook action count", std::to_string(actions.size()), "10");
    test_helpers::expect_eq("before hook action", std::to_string(static_cast<int>(btrfsbackup::backup::backup_run_action_kind(actions.at(1)))), std::to_string(static_cast<int>(btrfsbackup::backup::BackupRunActionKind::BeforeSnapshotHook)));
    test_helpers::expect_eq("before hook program", before_hook.hook.program, "/etc/btrfs-backup/hooks.d/before");
    test_helpers::expect_eq("before hook timeout", std::to_string(before_hook.hook.timeout.count()), "30");
    test_helpers::expect_eq("snapshot after before hook", std::to_string(static_cast<int>(btrfsbackup::backup::backup_run_action_kind(actions.at(2)))), std::to_string(static_cast<int>(btrfsbackup::backup::BackupRunActionKind::CreateSnapshot)));
    test_helpers::expect_eq("after hook action", std::to_string(static_cast<int>(btrfsbackup::backup::backup_run_action_kind(actions.at(3)))), std::to_string(static_cast<int>(btrfsbackup::backup::BackupRunActionKind::AfterSnapshotHook)));
    test_helpers::expect_eq("after hook argument", after_hook.hook.arguments.at(0), "root");
    test_helpers::expect_eq("after hook timeout", std::to_string(after_hook.hook.timeout.count()), "60");
}

void test_plans_collision_suffix_and_retention() {
    btrfsbackup::config::Profile test_profile = profile();
    test_profile.sources.at(0).local_retention = 2;
    test_profile.sources.at(0).remote_retention = 2;
    test_profile.settings.incremental_required = false;

    btrfsbackup::backup::SnapshotInventoryBySource local{
        {
            btrfsbackup::SourceId{"root"},
            {
                snapshot(btrfsbackup::backup::SnapshotSide::Local, "root", "root-2026-08-21T080000Z", "2026-08-21T080000Z", "/.snapshots/root/old", "old"),
                snapshot(btrfsbackup::backup::SnapshotSide::Local, "root", "root-2026-08-23T080000Z", "2026-08-23T080000Z", "/.snapshots/root/current-name", "current-name"),
            },
        },
    };

    btrfsbackup::backup::BackupRunPlan plan = btrfsbackup::backup::build_backup_run_plan(
        test_profile,
        local,
        {},
        {},
        {},
        "/var/lib/btrfs-backup/profiles/default",
        btrfsbackup::RunId{"20260823T080000Z-123-456"},
        "2026-08-23T080000Z"
    );

    const btrfsbackup::backup::BackupSourceRunPlan& source = plan.sources.at(0);
    test_helpers::expect_eq("collision suffix", source.local_snapshot_path.filename().string(), "root-2026-08-23T080000Z-01");
    test_helpers::expect_eq("local retention deletes one", std::to_string(source.local_retention.delete_snapshots.size()), "1");
    test_helpers::expect_eq("local retention deletes old", source.local_retention.delete_snapshots.at(0).path.string(), "/.snapshots/root/old");
}

void test_includes_pending_recovery_action() {
    btrfsbackup::backup::PendingMarkerBySource markers{
        {
            btrfsbackup::SourceId{"root"},
            btrfsbackup::backup::PendingMarker{
                .source_name = "root",
                .local_snapshot_path = "/.snapshots/root/root-2026-08-22T080000Z",
                .final_snapshot_path = "/mnt/backup/snapshots/root/root-2026-08-22T080000Z",
                .run_id = "20260822T080000Z-123-456",
                .timestamp = "2026-08-22T08:00:00+00:00",
            },
        },
    };
    btrfsbackup::backup::PendingSnapshotBySource pending_snapshots{
        {
            btrfsbackup::SourceId{"root"},
            btrfsbackup::backup::SnapshotMetadata{
                .is_subvolume = true,
                .readonly = true,
                .uuid = "orphan-uuid",
            },
        },
    };

    btrfsbackup::config::Profile test_profile = profile();
    test_profile.settings.incremental_required = false;

    btrfsbackup::backup::BackupRunPlan plan = btrfsbackup::backup::build_backup_run_plan(
        test_profile,
        {},
        {},
        markers,
        pending_snapshots,
        "/var/lib/btrfs-backup/profiles/default",
        btrfsbackup::RunId{"20260823T080000Z-123-456"},
        "2026-08-23T080000Z"
    );

    const btrfsbackup::backup::BackupSourceRunPlan& source = plan.sources.at(0);
    test_helpers::expect_eq("recovery action", std::to_string(static_cast<int>(btrfsbackup::backup::backup_run_action_kind(source.actions.at(0)))), std::to_string(static_cast<int>(btrfsbackup::backup::BackupRunActionKind::RecoverPending)));
    test_helpers::expect_true("recovery delete", source.recovery.delete_local_snapshot, "orphan should be scheduled for deletion");
}

void test_excludes_recovery_deletions_from_retention() {
    const btrfsbackup::backup::SnapshotInfo orphan = snapshot(
        btrfsbackup::backup::SnapshotSide::Local,
        "root",
        "root-2026-08-20T080000Z",
        "2026-08-20T080000Z",
        "/.snapshots/root/root-2026-08-20T080000Z",
        "orphan-uuid"
    );
    btrfsbackup::backup::SnapshotInventoryBySource local{
        {
            btrfsbackup::SourceId{"root"},
            {
                orphan,
                snapshot(btrfsbackup::backup::SnapshotSide::Local, "root", "root-2026-08-21T080000Z", "2026-08-21T080000Z", "/.snapshots/root/one", "one"),
                snapshot(btrfsbackup::backup::SnapshotSide::Local, "root", "root-2026-08-22T080000Z", "2026-08-22T080000Z", "/.snapshots/root/two", "two"),
            },
        },
    };
    btrfsbackup::backup::PendingMarkerBySource markers{
        {
            btrfsbackup::SourceId{"root"},
            btrfsbackup::backup::PendingMarker{
                .source_name = "root",
                .local_snapshot_path = orphan.path.string(),
                .final_snapshot_path = "/mnt/backup/snapshots/root/root-2026-08-20T080000Z",
                .run_id = "20260820T080000Z-123-456",
                .timestamp = "2026-08-20T08:00:00+00:00",
            },
        },
    };
    btrfsbackup::backup::PendingSnapshotBySource pending_snapshots{
        {
            btrfsbackup::SourceId{"root"},
            btrfsbackup::backup::SnapshotMetadata{
                .is_subvolume = true,
                .readonly = true,
                .uuid = orphan.uuid,
            },
        },
    };

    btrfsbackup::config::Profile test_profile = profile();
    test_profile.settings.incremental_required = false;
    btrfsbackup::backup::BackupRunPlan plan = btrfsbackup::backup::build_backup_run_plan(
        test_profile,
        local,
        {},
        markers,
        pending_snapshots,
        "/var/lib/btrfs-backup/profiles/default",
        btrfsbackup::RunId{"20260823T080000Z-123-456"},
        "2026-08-23T080000Z"
    );

    const btrfsbackup::backup::BackupSourceRunPlan& source = plan.sources.at(0);
    test_helpers::expect_true("recovery deletes orphan", source.recovery.delete_local_snapshot, "orphan should be recovered");
    test_helpers::expect_eq("retention deletes one", std::to_string(source.local_retention.delete_snapshots.size()), "1");
    test_helpers::expect_true(
        "retention does not repeat recovery deletion",
        source.local_retention.delete_snapshots.at(0).path != orphan.path,
        "a snapshot must not be deleted by recovery and retention"
    );
}

void test_rejects_invalid_mount_layout() {
    btrfsbackup::config::Profile test_profile = profile();
    test_profile.sources.at(0).local_snapshot_dir = "/mnt/backup/local";

    test_helpers::expect_validation_error("target local dir", [&] { (void)btrfsbackup::backup::build_backup_run_plan(
                                                                        test_profile,
                                                                        {},
                                                                        {},
                                                                        {},
                                                                        {},
                                                                        "/var/lib/btrfs-backup/profiles/default",
                                                                        btrfsbackup::RunId{"20260823T080000Z-123-456"},
                                                                        "2026-08-23T080000Z"
                                                                    ); }, "LOCAL_SNAPSHOT_DIR must not be inside the backup target");
}

} // namespace

int main() {
    test_builds_ordered_source_plan();
    test_inserts_snapshot_hooks_around_snapshot_creation();
    test_plans_collision_suffix_and_retention();
    test_includes_pending_recovery_action();
    test_excludes_recovery_deletions_from_retention();
    test_rejects_invalid_mount_layout();

    return test_helpers::finish("backup run plan tests");
}
