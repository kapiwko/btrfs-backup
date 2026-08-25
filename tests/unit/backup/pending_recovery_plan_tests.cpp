// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <backup/pending_recovery_plan.hpp>
#include <state/run_state.hpp>

#include "support/test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

btrfsbackup::SnapshotInfo remote_snapshot(const std::string& source_id, const std::string& received_uuid) {
    return btrfsbackup::SnapshotInfo{
        .side = btrfsbackup::SnapshotSide::Remote,
        .source_id = source_id,
        .name = source_id + "-2026-08-23T080000Z",
        .timestamp = "2026-08-23T080000Z",
        .sequence = 0,
        .path = "/remote/" + source_id + "/" + source_id + "-2026-08-23T080000Z",
        .readonly = true,
        .uuid = "remote-uuid",
        .received_uuid = received_uuid,
    };
}

btrfsbackup::PendingMarker marker(const std::string& source_id, const std::string& path) {
    const fs::path local_path(path);
    return btrfsbackup::PendingMarker{
        .source_name = source_id,
        .local_snapshot_path = path,
        .final_snapshot_path = (fs::path("/remote") / source_id / local_path.filename()).string(),
        .run_id = "20260823T080000Z-123-456",
        .timestamp = "2026-08-23T08:00:00+00:00",
    };
}

btrfsbackup::SnapshotMetadata local_snapshot(const std::string& uuid) {
    return btrfsbackup::SnapshotMetadata{
        .is_subvolume = true,
        .readonly = true,
        .uuid = uuid,
    };
}

void test_reads_pending_marker() {
    fs::path root = test_helpers::test_root("pending-recovery", "read");
    fs::path state_dir = root / "state" / "profiles" / "default";

    btrfsbackup::write_pending_marker(
        state_dir,
        marker("root", "/local/root/root-2026-08-23T080000Z")
    );

    std::optional<btrfsbackup::PendingMarker> read =
        btrfsbackup::read_pending_marker_if_exists(state_dir, "root");

    test_helpers::expect_true("read pending marker", read.has_value(), "expected pending marker");
    test_helpers::expect_eq("read pending source", read->source_name, "root");
    test_helpers::expect_eq("read pending path", read->local_snapshot_path, "/local/root/root-2026-08-23T080000Z");

    std::optional<btrfsbackup::PendingMarker> missing =
        btrfsbackup::read_pending_marker_if_exists(state_dir, "home");
    test_helpers::expect_true("missing pending marker", !missing.has_value(), "missing marker should return nullopt");

    fs::remove_all(root);
}

void test_no_marker_does_nothing() {
    btrfsbackup::PendingRecoveryPlan plan = btrfsbackup::plan_pending_recovery(
        "root",
        "/state/default",
        "/local/root",
        "/remote/root",
        std::nullopt,
        std::nullopt,
        {},
        false
    );

    test_helpers::expect_eq("no marker action", std::to_string(static_cast<int>(plan.action)), std::to_string(static_cast<int>(btrfsbackup::PendingRecoveryAction::NoMarker)));
    test_helpers::expect_true("no marker clear", !plan.clear_marker, "no marker should not clear anything");
    test_helpers::expect_true("no marker delete", !plan.delete_local_snapshot, "no marker should not delete anything");
}

void test_invalid_marker_is_cleared() {
    btrfsbackup::PendingRecoveryPlan plan = btrfsbackup::plan_pending_recovery(
        "root",
        "/state/default",
        "/local/root",
        "/remote/root",
        marker("root", "/outside/root-2026-08-23T080000Z"),
        std::nullopt,
        {},
        false
    );

    test_helpers::expect_eq("invalid marker action", std::to_string(static_cast<int>(plan.action)), std::to_string(static_cast<int>(btrfsbackup::PendingRecoveryAction::ClearInvalidMarker)));
    test_helpers::expect_true("invalid marker clear", plan.clear_marker, "invalid marker should be cleared");
    test_helpers::expect_true("invalid marker no delete", !plan.delete_local_snapshot, "invalid marker should not delete outside path");
}

void test_missing_snapshot_clears_marker() {
    btrfsbackup::PendingRecoveryPlan plan = btrfsbackup::plan_pending_recovery(
        "root",
        "/state/default",
        "/local/root",
        "/remote/root",
        marker("root", "/local/root/root-2026-08-23T080000Z"),
        std::nullopt,
        {},
        false
    );

    test_helpers::expect_eq("missing snapshot action", std::to_string(static_cast<int>(plan.action)), std::to_string(static_cast<int>(btrfsbackup::PendingRecoveryAction::ClearMissingSnapshot)));
    test_helpers::expect_true("missing snapshot clear", plan.clear_marker, "missing snapshot marker should be cleared");
    test_helpers::expect_true("missing snapshot no delete", !plan.delete_local_snapshot, "missing snapshot cannot be deleted");
}

void test_preserves_committed_snapshot() {
    btrfsbackup::PendingRecoveryPlan plan = btrfsbackup::plan_pending_recovery(
        "home",
        "/state/default",
        "/local/home",
        "/remote/home",
        marker("home", "/local/home/home-2026-08-23T080000Z"),
        local_snapshot("AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA"),
        {remote_snapshot("home", "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa")},
        false
    );

    test_helpers::expect_eq("committed action", std::to_string(static_cast<int>(plan.action)), std::to_string(static_cast<int>(btrfsbackup::PendingRecoveryAction::PreserveCommittedSnapshot)));
    test_helpers::expect_true("committed clear", plan.clear_marker, "committed marker should be cleared");
    test_helpers::expect_true("committed no delete", !plan.delete_local_snapshot, "committed parent should be preserved");
}

void test_removes_invalid_snapshot_left_at_final_path() {
    btrfsbackup::PendingRecoveryPlan plan = btrfsbackup::plan_pending_recovery(
        "home",
        "/state/default",
        "/local/home",
        "/remote/home",
        marker("home", "/local/home/home-2026-08-23T080000Z"),
        local_snapshot("expected-uuid"),
        {remote_snapshot("home", "wrong-uuid")},
        false
    );

    test_helpers::expect_eq(
        "invalid committed action",
        std::to_string(static_cast<int>(plan.action)),
        std::to_string(static_cast<int>(btrfsbackup::PendingRecoveryAction::DeleteInvalidCommittedSnapshot))
    );
    test_helpers::expect_true("invalid committed remote delete", plan.delete_remote_snapshot, "invalid final snapshot should be deleted");
    test_helpers::expect_true("invalid committed local delete", plan.delete_local_snapshot, "orphaned local snapshot should be deleted by policy");
    test_helpers::expect_eq(
        "invalid committed path",
        plan.remote_snapshot_path.string(),
        "/remote/home/home-2026-08-23T080000Z"
    );
}

void test_legacy_marker_without_final_path_uses_uuid_recovery() {
    btrfsbackup::PendingMarker legacy = marker("home", "/local/home/home-2026-08-23T080000Z");
    legacy.final_snapshot_path.clear();

    btrfsbackup::PendingRecoveryPlan plan = btrfsbackup::plan_pending_recovery(
        "home",
        "/state/default",
        "/local/home",
        "/remote/home",
        legacy,
        local_snapshot("expected-uuid"),
        {remote_snapshot("home", "expected-uuid")},
        false
    );

    test_helpers::expect_eq(
        "legacy marker action",
        std::to_string(static_cast<int>(plan.action)),
        std::to_string(static_cast<int>(btrfsbackup::PendingRecoveryAction::PreserveCommittedSnapshot))
    );
    test_helpers::expect_true("legacy marker no remote delete", !plan.delete_remote_snapshot, "legacy marker has no trusted final path");
}

void test_keeps_or_deletes_orphan_by_policy() {
    btrfsbackup::PendingRecoveryPlan keep = btrfsbackup::plan_pending_recovery(
        "home",
        "/state/default",
        "/local/home",
        "/remote/home",
        marker("home", "/local/home/home-2026-08-23T080000Z"),
        local_snapshot("local-only"),
        {},
        true
    );
    test_helpers::expect_eq("keep orphan action", std::to_string(static_cast<int>(keep.action)), std::to_string(static_cast<int>(btrfsbackup::PendingRecoveryAction::KeepFailedLocalSnapshot)));
    test_helpers::expect_true("keep orphan no delete", !keep.delete_local_snapshot, "configured keep should not delete");

    btrfsbackup::PendingRecoveryPlan remove = btrfsbackup::plan_pending_recovery(
        "home",
        "/state/default",
        "/local/home",
        "/remote/home",
        marker("home", "/local/home/home-2026-08-23T080000Z"),
        local_snapshot("local-only"),
        {},
        false
    );
    test_helpers::expect_eq("delete orphan action", std::to_string(static_cast<int>(remove.action)), std::to_string(static_cast<int>(btrfsbackup::PendingRecoveryAction::DeleteOrphanSnapshot)));
    test_helpers::expect_true("delete orphan", remove.delete_local_snapshot, "orphan should be deleted by default");
}

} // namespace

int main() {
    test_reads_pending_marker();
    test_no_marker_does_nothing();
    test_invalid_marker_is_cleared();
    test_missing_snapshot_clears_marker();
    test_preserves_committed_snapshot();
    test_removes_invalid_snapshot_left_at_final_path();
    test_legacy_marker_without_final_path_uses_uuid_recovery();
    test_keeps_or_deletes_orphan_by_policy();

    return test_helpers::finish("pending recovery plan tests");
}
