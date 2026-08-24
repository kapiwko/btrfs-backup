#include <filesystem>
#include <string>
#include <vector>

#include <btrfsbackup/backup_run_plan.hpp>

#include "test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

btrfsbackup::Profile profile() {
    btrfsbackup::Profile result;
    result.id = "default";
    result.name = "Default backup";
    result.target.mapper_name = "backup";
    result.target.mount_point = "/mnt/backup";
    result.target.btrfs_uuid = "target-fs";
    result.paths.remote_root = "/mnt/backup/snapshots";
    result.paths.incoming_root = "/mnt/backup/.incoming";
    result.paths.state_dir = "/var/lib/btrfs-backup";
    result.settings.incremental_required = true;
    result.settings.keep_failed_local_snapshot = false;
    result.sources = {
        {
            .id = "root",
            .name = "System",
            .enabled = true,
            .subvolume = "/",
            .local_snapshot_dir = "/.snapshots/root",
            .remote_subdir = "root",
            .remote_retention = 2,
            .local_retention = 2,
        },
        {
            .id = "home",
            .name = "Home",
            .enabled = false,
            .subvolume = "/home",
            .local_snapshot_dir = "/.snapshots/home",
            .remote_subdir = "home",
            .remote_retention = 2,
            .local_retention = 2,
        },
    };
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
        "20260823T080000Z-123-456",
        "2026-08-23T080000Z"
    );

    test_helpers::expect_eq("plan source count", std::to_string(plan.sources.size()), "1");
    const btrfsbackup::BackupSourceRunPlan& source = plan.sources.at(0);
    test_helpers::expect_eq("plan source id", source.source_id, "root");
    test_helpers::expect_eq("plan snapshot path", source.local_snapshot_path.string(), "/.snapshots/root/root-2026-08-23T080000Z");
    test_helpers::expect_true("plan incremental", source.parent.incremental, "expected incremental parent");
    test_helpers::expect_eq("plan parent", source.parent.local_parent->path.string(), "/.snapshots/root/root-2026-08-22T080000Z");
    test_helpers::expect_eq("plan actions", std::to_string(source.actions.size()), "9");
    test_helpers::expect_eq("first action", std::to_string(static_cast<int>(source.actions.at(0).kind)), std::to_string(static_cast<int>(btrfsbackup::BackupRunActionKind::CleanupIncoming)));
    test_helpers::expect_eq("last action", std::to_string(static_cast<int>(source.actions.back().kind)), std::to_string(static_cast<int>(btrfsbackup::BackupRunActionKind::CleanupSource)));
}

void test_inserts_snapshot_hooks_around_snapshot_creation() {
    btrfsbackup::Profile test_profile = profile();
    test_profile.settings.incremental_required = false;
    test_profile.hooks.before_snapshot = {
        btrfsbackup::ProfileHookCommand{
            .program = "/usr/local/bin/before",
            .arguments = {"root"},
        },
    };
    test_profile.hooks.after_snapshot = {
        btrfsbackup::ProfileHookCommand{
            .program = "/usr/local/bin/after",
            .arguments = {"root"},
        },
    };

    btrfsbackup::BackupRunPlan plan = btrfsbackup::build_backup_run_plan(
        test_profile,
        mounts(),
        {},
        {},
        {},
        {},
        "20260823T080000Z-123-456",
        "2026-08-23T080000Z"
    );

    const std::vector<btrfsbackup::BackupRunAction>& actions = plan.sources.at(0).actions;
    test_helpers::expect_eq("hook action count", std::to_string(actions.size()), "11");
    test_helpers::expect_eq("before hook action", std::to_string(static_cast<int>(actions.at(1).kind)), std::to_string(static_cast<int>(btrfsbackup::BackupRunActionKind::BeforeSnapshotHook)));
    test_helpers::expect_eq("before hook program", actions.at(1).hook.program, "/usr/local/bin/before");
    test_helpers::expect_eq("snapshot after before hook", std::to_string(static_cast<int>(actions.at(2).kind)), std::to_string(static_cast<int>(btrfsbackup::BackupRunActionKind::CreateSnapshot)));
    test_helpers::expect_eq("after hook action", std::to_string(static_cast<int>(actions.at(3).kind)), std::to_string(static_cast<int>(btrfsbackup::BackupRunActionKind::AfterSnapshotHook)));
    test_helpers::expect_eq("after hook argument", actions.at(3).hook.arguments.at(0), "root");
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
        "20260823T080000Z-123-456",
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
        "20260823T080000Z-123-456",
        "2026-08-23T080000Z"
    );

    const btrfsbackup::BackupSourceRunPlan& source = plan.sources.at(0);
    test_helpers::expect_eq("recovery action", std::to_string(static_cast<int>(source.actions.at(0).kind)), std::to_string(static_cast<int>(btrfsbackup::BackupRunActionKind::RecoverPending)));
    test_helpers::expect_true("recovery delete", source.recovery.delete_local_snapshot, "orphan should be scheduled for deletion");
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
            "20260823T080000Z-123-456",
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
    test_rejects_invalid_mount_layout();

    return test_helpers::finish("backup run plan tests");
}
