// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <backup/backup_run_plan.hpp>

#include "support/validation_test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

static_assert(!std::is_default_constructible_v<btrfsbackup::CreateSnapshotAction>);
static_assert(!std::is_default_constructible_v<btrfsbackup::BackupRunAction>);

btrfsbackup::Profile profile() {
    btrfsbackup::Profile result{btrfsbackup::ProfileId{"default"}};
    result.name = "Default backup";
    result.target.mapper_name = "backup";
    result.target.mount_point = "/mnt/backup";
    result.target.btrfs_uuid = "target-fs";
    result.paths.remote_root = "/mnt/backup/snapshots";
    result.paths.incoming_root = "/mnt/backup/.incoming";
    result.settings.incremental_required = true;
    result.settings.keep_failed_local_snapshot = false;
    btrfsbackup::ProfileSource root{btrfsbackup::SourceId{"root"}};
    root.name = "System";
    root.subvolume = "/";
    root.local_snapshot_dir = "/.snapshots/root";
    root.remote_subdir = "root";
    root.remote_retention = 2;
    root.local_retention = 2;
    btrfsbackup::ProfileSource home{btrfsbackup::SourceId{"home"}};
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

std::vector<btrfsbackup::MountEntry> mounts() {
    return {
        {
            .source = "/dev/source",
            .target = "/",
            .fstype = "btrfs",
            .device_id = "0:20",
            .filesystem_uuid = "source-fs",
        },
        {
            .source = "/dev/source",
            .target = "/.snapshots",
            .fstype = "btrfs",
            .device_id = "0:20",
            .filesystem_uuid = "source-fs",
        },
        {
            .source = "/dev/mapper/backup",
            .target = "/mnt/backup",
            .fstype = "btrfs",
            .device_id = "0:21",
            .filesystem_uuid = "target-fs",
        },
    };
}

btrfsbackup::SnapshotInfo snapshot(
    btrfsbackup::SnapshotSide side,
    const std::string& source_id,
    const std::string& name,
    const std::string& timestamp,
    const fs::path& path,
    const std::string& uuid,
    const std::string& received_uuid = ""
) {
    return btrfsbackup::SnapshotInfo{
        .side = side,
        .source_id = source_id,
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
    btrfsbackup::SnapshotInventoryBySource local{
        {
            "root",
            {
                snapshot(
                    btrfsbackup::SnapshotSide::Local,
                    "root",
                    "root-2026-08-22T080000Z",
                    "2026-08-22T080000Z",
                    "/.snapshots/root/root-2026-08-22T080000Z",
                    "parent-uuid"
                ),
            },
        },
    };
    btrfsbackup::SnapshotInventoryBySource remote{
        {
            "root",
            {
                snapshot(
                    btrfsbackup::SnapshotSide::Remote,
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

    btrfsbackup::BackupRunPlan plan = btrfsbackup::build_backup_run_plan(
        profile(),
        mounts(),
        local,
        remote,
        {},
        {},
        "/var/lib/btrfs-backup/profiles/default",
        btrfsbackup::RunId{"20260823T080000Z-123-456"},
        "2026-08-23T080000Z"
    );

    test_helpers::expect_eq("plan source count", std::to_string(plan.sources.size()), "1");
    const btrfsbackup::BackupSourceRunPlan& source = plan.sources.at(0);
    test_helpers::expect_eq("plan source id", std::string(source.source_id.value()), "root");
    test_helpers::expect_eq("plan snapshot path", source.local_snapshot_path.string(), "/.snapshots/root/root-2026-08-23T080000Z");
    test_helpers::expect_true("plan incremental", source.parent.incremental, "expected incremental parent");
    test_helpers::expect_eq("plan parent", source.parent.local_parent->path.string(), "/.snapshots/root/root-2026-08-22T080000Z");
    test_helpers::expect_eq("plan actions", std::to_string(source.actions.size()), "9");
    test_helpers::expect_eq("first action", std::to_string(static_cast<int>(btrfsbackup::backup_run_action_kind(source.actions.at(0)))), std::to_string(static_cast<int>(btrfsbackup::BackupRunActionKind::CleanupIncoming)));
    test_helpers::expect_eq("last action", std::to_string(static_cast<int>(btrfsbackup::backup_run_action_kind(source.actions.back()))), std::to_string(static_cast<int>(btrfsbackup::BackupRunActionKind::CleanupSource)));

    const auto& create_snapshot = std::get<btrfsbackup::CreateSnapshotAction>(source.actions.at(1));
    test_helpers::expect_eq("create source", create_snapshot.source.string(), "/");
    test_helpers::expect_eq("create snapshot", create_snapshot.snapshot.string(), "/.snapshots/root/root-2026-08-23T080000Z");
    test_helpers::expect_eq("create run id", std::string(create_snapshot.run_id.value()), "20260823T080000Z-123-456");

    const auto& send_receive = std::get<btrfsbackup::SendReceiveAction>(source.actions.at(3));
    test_helpers::expect_eq("send snapshot", send_receive.snapshot.string(), create_snapshot.snapshot.string());
    test_helpers::expect_eq("send parent", send_receive.parent->string(), "/.snapshots/root/root-2026-08-22T080000Z");
    test_helpers::expect_eq("receive directory", send_receive.incoming_run_directory.string(), "/mnt/backup/.incoming/root/20260823T080000Z-123-456");
}

void test_inserts_snapshot_hooks_around_snapshot_creation() {
    btrfsbackup::Profile test_profile = profile();
    test_profile.settings.incremental_required = false;
    test_profile.hooks.before_snapshot = {
        btrfsbackup::ProfileHookCommand{
            .program = "/etc/btrfs-backup/hooks.d/before",
            .arguments = {"root"},
            .timeout = std::chrono::seconds{30},
        },
    };
    test_profile.hooks.after_snapshot = {
        btrfsbackup::ProfileHookCommand{
            .program = "/etc/btrfs-backup/hooks.d/after",
            .arguments = {"root"},
            .timeout = std::chrono::seconds{60},
        },
    };

    btrfsbackup::BackupRunPlan plan = btrfsbackup::build_backup_run_plan(
        test_profile,
        mounts(),
        {},
        {},
        {},
        {},
        "/var/lib/btrfs-backup/profiles/default",
        btrfsbackup::RunId{"20260823T080000Z-123-456"},
        "2026-08-23T080000Z"
    );

    const std::vector<btrfsbackup::BackupRunAction>& actions = plan.sources.at(0).actions;
    const auto& before_hook = std::get<btrfsbackup::RunHookAction>(actions.at(1));
    const auto& after_hook = std::get<btrfsbackup::RunHookAction>(actions.at(3));
    test_helpers::expect_eq("hook action count", std::to_string(actions.size()), "11");
    test_helpers::expect_eq("before hook action", std::to_string(static_cast<int>(btrfsbackup::backup_run_action_kind(actions.at(1)))), std::to_string(static_cast<int>(btrfsbackup::BackupRunActionKind::BeforeSnapshotHook)));
    test_helpers::expect_eq("before hook program", before_hook.hook.program, "/etc/btrfs-backup/hooks.d/before");
    test_helpers::expect_eq("before hook timeout", std::to_string(before_hook.hook.timeout.count()), "30");
    test_helpers::expect_eq("snapshot after before hook", std::to_string(static_cast<int>(btrfsbackup::backup_run_action_kind(actions.at(2)))), std::to_string(static_cast<int>(btrfsbackup::BackupRunActionKind::CreateSnapshot)));
    test_helpers::expect_eq("after hook action", std::to_string(static_cast<int>(btrfsbackup::backup_run_action_kind(actions.at(3)))), std::to_string(static_cast<int>(btrfsbackup::BackupRunActionKind::AfterSnapshotHook)));
    test_helpers::expect_eq("after hook argument", after_hook.hook.arguments.at(0), "root");
    test_helpers::expect_eq("after hook timeout", std::to_string(after_hook.hook.timeout.count()), "60");
}

void test_plans_collision_suffix_and_retention() {
    btrfsbackup::Profile test_profile = profile();
    test_profile.sources.at(0).local_retention = 2;
    test_profile.sources.at(0).remote_retention = 2;
    test_profile.settings.incremental_required = false;

    btrfsbackup::SnapshotInventoryBySource local{
        {
            "root",
            {
                snapshot(btrfsbackup::SnapshotSide::Local, "root", "root-2026-08-21T080000Z", "2026-08-21T080000Z", "/.snapshots/root/old", "old"),
                snapshot(btrfsbackup::SnapshotSide::Local, "root", "root-2026-08-23T080000Z", "2026-08-23T080000Z", "/.snapshots/root/current-name", "current-name"),
            },
        },
    };

    btrfsbackup::BackupRunPlan plan = btrfsbackup::build_backup_run_plan(
        test_profile,
        mounts(),
        local,
        {},
        {},
        {},
        "/var/lib/btrfs-backup/profiles/default",
        btrfsbackup::RunId{"20260823T080000Z-123-456"},
        "2026-08-23T080000Z"
    );

    const btrfsbackup::BackupSourceRunPlan& source = plan.sources.at(0);
    test_helpers::expect_eq("collision suffix", source.local_snapshot_path.filename().string(), "root-2026-08-23T080000Z-01");
    test_helpers::expect_eq("local retention deletes one", std::to_string(source.local_retention.delete_snapshots.size()), "1");
    test_helpers::expect_eq("local retention deletes old", source.local_retention.delete_snapshots.at(0).path.string(), "/.snapshots/root/old");
}

void test_includes_pending_recovery_action() {
    btrfsbackup::PendingMarkerBySource markers{
        {
            "root",
            btrfsbackup::PendingMarker{
                .source_name = "root",
                .local_snapshot_path = "/.snapshots/root/root-2026-08-22T080000Z",
                .final_snapshot_path = "/mnt/backup/snapshots/root/root-2026-08-22T080000Z",
                .run_id = "20260822T080000Z-123-456",
                .timestamp = "2026-08-22T08:00:00+00:00",
            },
        },
    };
    btrfsbackup::PendingSnapshotBySource pending_snapshots{
        {
            "root",
            btrfsbackup::SnapshotMetadata{
                .is_subvolume = true,
                .readonly = true,
                .uuid = "orphan-uuid",
            },
        },
    };

    btrfsbackup::Profile test_profile = profile();
    test_profile.settings.incremental_required = false;

    btrfsbackup::BackupRunPlan plan = btrfsbackup::build_backup_run_plan(
        test_profile,
        mounts(),
        {},
        {},
        markers,
        pending_snapshots,
        "/var/lib/btrfs-backup/profiles/default",
        btrfsbackup::RunId{"20260823T080000Z-123-456"},
        "2026-08-23T080000Z"
    );

    const btrfsbackup::BackupSourceRunPlan& source = plan.sources.at(0);
    test_helpers::expect_eq("recovery action", std::to_string(static_cast<int>(btrfsbackup::backup_run_action_kind(source.actions.at(0)))), std::to_string(static_cast<int>(btrfsbackup::BackupRunActionKind::RecoverPending)));
    test_helpers::expect_true("recovery delete", source.recovery.delete_local_snapshot, "orphan should be scheduled for deletion");
}

void test_excludes_recovery_deletions_from_retention() {
    const btrfsbackup::SnapshotInfo orphan = snapshot(
        btrfsbackup::SnapshotSide::Local,
        "root",
        "root-2026-08-20T080000Z",
        "2026-08-20T080000Z",
        "/.snapshots/root/root-2026-08-20T080000Z",
        "orphan-uuid"
    );
    btrfsbackup::SnapshotInventoryBySource local{
        {
            "root",
            {
                orphan,
                snapshot(btrfsbackup::SnapshotSide::Local, "root", "root-2026-08-21T080000Z", "2026-08-21T080000Z", "/.snapshots/root/one", "one"),
                snapshot(btrfsbackup::SnapshotSide::Local, "root", "root-2026-08-22T080000Z", "2026-08-22T080000Z", "/.snapshots/root/two", "two"),
            },
        },
    };
    btrfsbackup::PendingMarkerBySource markers{
        {
            "root",
            btrfsbackup::PendingMarker{
                .source_name = "root",
                .local_snapshot_path = orphan.path.string(),
                .final_snapshot_path = "/mnt/backup/snapshots/root/root-2026-08-20T080000Z",
                .run_id = "20260820T080000Z-123-456",
                .timestamp = "2026-08-20T08:00:00+00:00",
            },
        },
    };
    btrfsbackup::PendingSnapshotBySource pending_snapshots{
        {
            "root",
            btrfsbackup::SnapshotMetadata{
                .is_subvolume = true,
                .readonly = true,
                .uuid = orphan.uuid,
            },
        },
    };

    btrfsbackup::Profile test_profile = profile();
    test_profile.settings.incremental_required = false;
    btrfsbackup::BackupRunPlan plan = btrfsbackup::build_backup_run_plan(
        test_profile,
        mounts(),
        local,
        {},
        markers,
        pending_snapshots,
        "/var/lib/btrfs-backup/profiles/default",
        btrfsbackup::RunId{"20260823T080000Z-123-456"},
        "2026-08-23T080000Z"
    );

    const btrfsbackup::BackupSourceRunPlan& source = plan.sources.at(0);
    test_helpers::expect_true("recovery deletes orphan", source.recovery.delete_local_snapshot, "orphan should be recovered");
    test_helpers::expect_eq("retention deletes one", std::to_string(source.local_retention.delete_snapshots.size()), "1");
    test_helpers::expect_true(
        "retention does not repeat recovery deletion",
        source.local_retention.delete_snapshots.at(0).path != orphan.path,
        "a snapshot must not be deleted by recovery and retention"
    );
}

void test_rejects_invalid_mount_layout() {
    btrfsbackup::Profile test_profile = profile();
    test_profile.sources.at(0).local_snapshot_dir = "/mnt/backup/local";

    test_helpers::expect_validation_error("target local dir", [&] {
        (void)btrfsbackup::build_backup_run_plan(
            test_profile,
            mounts(),
            {},
            {},
            {},
            {},
            "/var/lib/btrfs-backup/profiles/default",
            btrfsbackup::RunId{"20260823T080000Z-123-456"},
            "2026-08-23T080000Z"
        );
    }, "LOCAL_SNAPSHOT_DIR must be on the same Btrfs filesystem as /");
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
