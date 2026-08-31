// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <backup/model/PendingRecovery.hpp>
#include <platform/linux/filesystem/PosixDurableFileOperations.hpp>
#include <state/persistence/FilePendingMarkerStore.hpp>

#include "support/TestHelpers.hpp"

namespace fs = std::filesystem;

namespace {

btrfsbackup::backup::SnapshotInfo remote_snapshot(const std::string& source_id, const std::string& received_uuid) {
    return btrfsbackup::backup::SnapshotInfo{
        .side = btrfsbackup::backup::SnapshotSide::Remote,
        .source_id = btrfsbackup::SourceId{source_id},
        .name = source_id + "-2026-08-23T080000Z",
        .timestamp = test_helpers::runtime_time("2026-08-23T080000Z"),
        .sequence = 0,
        .path = "/remote/" + source_id + "/" + source_id + "-2026-08-23T080000Z",
        .readonly = true,
        .uuid = btrfsbackup::backup::SnapshotUuid{"remote-uuid"},
        .received_uuid = btrfsbackup::backup::ReceivedSnapshotUuid{received_uuid},
    };
}

btrfsbackup::backup::PendingMarker marker(const std::string& source_id, const std::string& path) {
    const fs::path local_path(path);
    return btrfsbackup::backup::PendingMarker{
        .source_id = btrfsbackup::SourceId{source_id},
        .local_snapshot_path = path,
        .final_snapshot_path = (fs::path("/remote") / source_id / local_path.filename()).string(),
        .run_id = btrfsbackup::RunId{"20260823T080000Z-123-456"},
        .timestamp = test_helpers::runtime_time("2026-08-23T08:00:00Z"),
    };
}

btrfsbackup::backup::SnapshotMetadata local_snapshot(const std::string& uuid) {
    return btrfsbackup::backup::SnapshotMetadata{
        .is_subvolume = true,
        .readonly = true,
        .uuid = btrfsbackup::backup::SnapshotUuid{uuid},
    };
}

template <typename Effect>
bool has_effect(const btrfsbackup::backup::PendingRecoveryPlan& plan) {
    return btrfsbackup::backup::pending_recovery_effect<Effect>(plan) != nullptr;
}

void test_reads_pending_marker() {
    fs::path root = test_helpers::test_root("pending-recovery", "read");
    fs::path state_dir = root / "state" / "profiles" / "default";

    btrfsbackup::platform::linux::filesystem::PosixDurableFileOperations durable_files;
    btrfsbackup::state::FilePendingMarkerStore markers(durable_files);
    markers.write(state_dir, marker("root", "/local/root/root-2026-08-23T080000Z"));

    std::optional<btrfsbackup::backup::PendingMarker> read =
        markers.read(state_dir, btrfsbackup::SourceId{"root"});

    test_helpers::expect_true("read pending marker", read.has_value(), "expected pending marker");
    test_helpers::expect_eq("read pending source", std::string(read->source_id.value()), "root");
    test_helpers::expect_eq("read pending path", read->local_snapshot_path.string(), "/local/root/root-2026-08-23T080000Z");

    std::optional<btrfsbackup::backup::PendingMarker> missing =
        markers.read(state_dir, btrfsbackup::SourceId{"home"});
    test_helpers::expect_true("missing pending marker", !missing.has_value(), "missing marker should return nullopt");

    fs::remove_all(root);
}

void test_no_marker_does_nothing() {
    btrfsbackup::backup::PendingRecoveryPlan plan = btrfsbackup::backup::plan_pending_recovery(
        btrfsbackup::SourceId{"root"},
        "/state/default",
        "/local/root",
        "/remote/root",
        std::nullopt,
        std::nullopt,
        {},
        false
    );

    test_helpers::expect_true("no marker recovery", !plan.required(), "no marker should not schedule recovery");
}

void test_invalid_marker_is_cleared() {
    btrfsbackup::backup::PendingRecoveryPlan plan = btrfsbackup::backup::plan_pending_recovery(
        btrfsbackup::SourceId{"root"},
        "/state/default",
        "/local/root",
        "/remote/root",
        marker("root", "/outside/root-2026-08-23T080000Z"),
        std::nullopt,
        {},
        false
    );

    test_helpers::expect_true("invalid marker clear", has_effect<btrfsbackup::backup::ClearPendingMarker>(plan), "invalid marker should be cleared");
    test_helpers::expect_true("invalid marker no delete", !has_effect<btrfsbackup::backup::DeletePendingLocalSnapshot>(plan), "invalid marker should not delete outside path");
}

void test_missing_snapshot_clears_marker() {
    btrfsbackup::backup::PendingRecoveryPlan plan = btrfsbackup::backup::plan_pending_recovery(
        btrfsbackup::SourceId{"root"},
        "/state/default",
        "/local/root",
        "/remote/root",
        marker("root", "/local/root/root-2026-08-23T080000Z"),
        std::nullopt,
        {},
        false
    );

    test_helpers::expect_true("missing snapshot clear", has_effect<btrfsbackup::backup::ClearPendingMarker>(plan), "missing snapshot marker should be cleared");
    test_helpers::expect_true("missing snapshot no delete", !has_effect<btrfsbackup::backup::DeletePendingLocalSnapshot>(plan), "missing snapshot cannot be deleted");
}

void test_preserves_committed_snapshot() {
    btrfsbackup::backup::PendingRecoveryPlan plan = btrfsbackup::backup::plan_pending_recovery(
        btrfsbackup::SourceId{"home"},
        "/state/default",
        "/local/home",
        "/remote/home",
        marker("home", "/local/home/home-2026-08-23T080000Z"),
        local_snapshot("AAAAAAAA-AAAA-AAAA-AAAA-AAAAAAAAAAAA"),
        {remote_snapshot("home", "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa")},
        false
    );

    test_helpers::expect_true("committed clear", has_effect<btrfsbackup::backup::ClearPendingMarker>(plan), "committed marker should be cleared");
    test_helpers::expect_true("committed no delete", !has_effect<btrfsbackup::backup::DeletePendingLocalSnapshot>(plan), "committed parent should be preserved");
}

void test_removes_invalid_snapshot_left_at_final_path() {
    btrfsbackup::backup::PendingRecoveryPlan plan = btrfsbackup::backup::plan_pending_recovery(
        btrfsbackup::SourceId{"home"},
        "/state/default",
        "/local/home",
        "/remote/home",
        marker("home", "/local/home/home-2026-08-23T080000Z"),
        local_snapshot("expected-uuid"),
        {remote_snapshot("home", "wrong-uuid")},
        false
    );

    test_helpers::expect_true("invalid committed remote delete", has_effect<btrfsbackup::backup::DeletePendingRemoteSnapshot>(plan), "invalid final snapshot should be deleted");
    test_helpers::expect_true("invalid committed local delete", has_effect<btrfsbackup::backup::DeletePendingLocalSnapshot>(plan), "orphaned local snapshot should be deleted by policy");
    const auto* remote_delete = btrfsbackup::backup::pending_recovery_effect<btrfsbackup::backup::DeletePendingRemoteSnapshot>(plan);
    test_helpers::expect_eq(
        "invalid committed path",
        remote_delete->snapshot_path.string(),
        "/remote/home/home-2026-08-23T080000Z"
    );
}

void test_marker_without_final_path_is_invalid() {
    btrfsbackup::backup::PendingMarker invalid = marker("home", "/local/home/home-2026-08-23T080000Z");
    invalid.final_snapshot_path.clear();

    btrfsbackup::backup::PendingRecoveryPlan plan = btrfsbackup::backup::plan_pending_recovery(
        btrfsbackup::SourceId{"home"},
        "/state/default",
        "/local/home",
        "/remote/home",
        invalid,
        local_snapshot("expected-uuid"),
        {remote_snapshot("home", "expected-uuid")},
        false
    );

    test_helpers::expect_true("invalid marker clear", has_effect<btrfsbackup::backup::ClearPendingMarker>(plan), "invalid marker should be cleared");
    test_helpers::expect_true("invalid marker no remote delete", !has_effect<btrfsbackup::backup::DeletePendingRemoteSnapshot>(plan), "invalid marker has no trusted final path");
    test_helpers::expect_contains("invalid marker message", plan.message, "Ignoring invalid pending marker");
}

void test_keeps_or_deletes_orphan_by_policy() {
    btrfsbackup::backup::PendingRecoveryPlan keep = btrfsbackup::backup::plan_pending_recovery(
        btrfsbackup::SourceId{"home"},
        "/state/default",
        "/local/home",
        "/remote/home",
        marker("home", "/local/home/home-2026-08-23T080000Z"),
        local_snapshot("local-only"),
        {},
        true
    );
    test_helpers::expect_true("keep orphan clear", has_effect<btrfsbackup::backup::ClearPendingMarker>(keep), "kept orphan marker should be cleared");
    test_helpers::expect_true("keep orphan no delete", !has_effect<btrfsbackup::backup::DeletePendingLocalSnapshot>(keep), "configured keep should not delete");

    btrfsbackup::backup::PendingRecoveryPlan remove = btrfsbackup::backup::plan_pending_recovery(
        btrfsbackup::SourceId{"home"},
        "/state/default",
        "/local/home",
        "/remote/home",
        marker("home", "/local/home/home-2026-08-23T080000Z"),
        local_snapshot("local-only"),
        {},
        false
    );
    test_helpers::expect_true("delete orphan", has_effect<btrfsbackup::backup::DeletePendingLocalSnapshot>(remove), "orphan should be deleted by default");
}

} // namespace

int main() {
    test_reads_pending_marker();
    test_no_marker_does_nothing();
    test_invalid_marker_is_cleared();
    test_missing_snapshot_clears_marker();
    test_preserves_committed_snapshot();
    test_removes_invalid_snapshot_left_at_final_path();
    test_marker_without_final_path_is_invalid();
    test_keeps_or_deletes_orphan_by_policy();

    return test_helpers::finish("pending recovery plan tests");
}
