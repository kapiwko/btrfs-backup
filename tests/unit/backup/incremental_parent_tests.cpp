#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <backup/incremental_parent.hpp>

#include "support/test_helpers.hpp"

namespace fs = std::filesystem;

namespace {

btrfsbackup::SnapshotInfo snapshot(
    btrfsbackup::SnapshotSide side,
    const std::string& source_id,
    const std::string& name,
    const std::string& timestamp,
    int sequence,
    const fs::path& path,
    bool readonly,
    const std::string& uuid,
    const std::string& received_uuid = ""
) {
    return btrfsbackup::SnapshotInfo{
        .side = side,
        .source_id = source_id,
        .name = name,
        .timestamp = timestamp,
        .sequence = sequence,
        .path = path,
        .readonly = readonly,
        .uuid = uuid,
        .received_uuid = received_uuid,
    };
}

void test_selects_newest_uuid_matching_parent() {
    std::vector<btrfsbackup::SnapshotInfo> local{
        snapshot(
            btrfsbackup::SnapshotSide::Local,
            "home",
            "home-2026-08-22T080000Z",
            "2026-08-22T080000Z",
            0,
            "/local/home/home-2026-08-22T080000Z",
            true,
            "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa"
        ),
        snapshot(
            btrfsbackup::SnapshotSide::Local,
            "home",
            "home-2026-08-23T080000Z",
            "2026-08-23T080000Z",
            0,
            "/local/home/home-2026-08-23T080000Z",
            true,
            "BBBBBBBB-BBBB-BBBB-BBBB-BBBBBBBBBBBB"
        ),
    };
    std::vector<btrfsbackup::SnapshotInfo> remote{
        snapshot(
            btrfsbackup::SnapshotSide::Remote,
            "home",
            "home-2026-08-22T080000Z",
            "2026-08-22T080000Z",
            0,
            "/remote/home/home-2026-08-22T080000Z",
            true,
            "remote-a",
            "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa"
        ),
        snapshot(
            btrfsbackup::SnapshotSide::Remote,
            "home",
            "home-2026-08-23T080000Z",
            "2026-08-23T080000Z",
            0,
            "/remote/home/home-2026-08-23T080000Z",
            true,
            "remote-b",
            "bbbbbbbb-bbbb-bbbb-bbbb-bbbbbbbbbbbb"
        ),
    };

    btrfsbackup::IncrementalParentSelection selection =
        btrfsbackup::select_incremental_parent("home", local, remote, std::nullopt, true);

    test_helpers::expect_true("select incremental", selection.incremental, "expected incremental parent");
    test_helpers::expect_true("select local parent", selection.local_parent.has_value(), "missing local parent");
    test_helpers::expect_true("select remote parent", selection.remote_parent.has_value(), "missing remote parent");
    test_helpers::expect_eq("select newest local", selection.local_parent->path.string(), "/local/home/home-2026-08-23T080000Z");
    test_helpers::expect_eq("select matching remote", selection.remote_parent->path.string(), "/remote/home/home-2026-08-23T080000Z");
}

void test_skips_current_snapshot() {
    std::vector<btrfsbackup::SnapshotInfo> local{
        snapshot(
            btrfsbackup::SnapshotSide::Local,
            "root",
            "root-2026-08-23T080000Z",
            "2026-08-23T080000Z",
            0,
            "/local/root/root-2026-08-23T080000Z",
            true,
            "current-uuid"
        ),
        snapshot(
            btrfsbackup::SnapshotSide::Local,
            "root",
            "root-2026-08-22T080000Z",
            "2026-08-22T080000Z",
            0,
            "/local/root/root-2026-08-22T080000Z",
            true,
            "parent-uuid"
        ),
    };
    std::vector<btrfsbackup::SnapshotInfo> remote{
        snapshot(
            btrfsbackup::SnapshotSide::Remote,
            "root",
            "root-2026-08-23T080000Z",
            "2026-08-23T080000Z",
            0,
            "/remote/root/root-2026-08-23T080000Z",
            true,
            "remote-current",
            "current-uuid"
        ),
        snapshot(
            btrfsbackup::SnapshotSide::Remote,
            "root",
            "root-2026-08-22T080000Z",
            "2026-08-22T080000Z",
            0,
            "/remote/root/root-2026-08-22T080000Z",
            true,
            "remote-parent",
            "parent-uuid"
        ),
    };

    btrfsbackup::IncrementalParentSelection selection =
        btrfsbackup::select_incremental_parent("root", local, remote, fs::path("/local/root/root-2026-08-23T080000Z"), true);

    test_helpers::expect_true("skip current incremental", selection.incremental, "expected older parent");
    test_helpers::expect_eq("skip current local", selection.local_parent->path.string(), "/local/root/root-2026-08-22T080000Z");
}

void test_missing_parent_rules() {
    std::vector<btrfsbackup::SnapshotInfo> local{
        snapshot(
            btrfsbackup::SnapshotSide::Local,
            "home",
            "home-2026-08-23T080000Z",
            "2026-08-23T080000Z",
            0,
            "/local/home/home-2026-08-23T080000Z",
            true,
            "local-only"
        ),
    };
    std::vector<btrfsbackup::SnapshotInfo> remote{
        snapshot(
            btrfsbackup::SnapshotSide::Remote,
            "home",
            "home-2026-08-22T080000Z",
            "2026-08-22T080000Z",
            0,
            "/remote/home/home-2026-08-22T080000Z",
            true,
            "remote-only",
            "remote-only"
        ),
    };

    btrfsbackup::IncrementalParentSelection full =
        btrfsbackup::select_incremental_parent("home", local, remote, std::nullopt, false);
    test_helpers::expect_true("missing parent full", !full.incremental, "optional incremental should fall back to full");
    test_helpers::expect_true("remote snapshots exist", full.remote_snapshots_exist, "remote inventory should be noted");

    test_helpers::expect_validation_error("missing required parent", [&] {
        (void)btrfsbackup::select_incremental_parent("home", local, remote, std::nullopt, true);
    }, "Remote snapshots exist for home, but no UUID-matching local parent was found.");
}

void test_ignores_unusable_snapshots() {
    std::vector<btrfsbackup::SnapshotInfo> local{
        snapshot(
            btrfsbackup::SnapshotSide::Local,
            "home",
            "home-2026-08-23T080000Z",
            "2026-08-23T080000Z",
            0,
            "/local/home/not-readonly",
            false,
            "matched"
        ),
        snapshot(
            btrfsbackup::SnapshotSide::Local,
            "home",
            "home-2026-08-22T080000Z",
            "2026-08-22T080000Z",
            0,
            "/local/home/no-uuid",
            true,
            ""
        ),
    };
    std::vector<btrfsbackup::SnapshotInfo> remote{
        snapshot(
            btrfsbackup::SnapshotSide::Remote,
            "home",
            "home-2026-08-22T080000Z",
            "2026-08-22T080000Z",
            0,
            "/remote/home/home-2026-08-22T080000Z",
            false,
            "remote",
            "matched"
        ),
    };

    btrfsbackup::IncrementalParentSelection selection =
        btrfsbackup::select_incremental_parent("home", local, remote, std::nullopt, false);
    test_helpers::expect_true("ignore unusable", !selection.incremental, "unusable snapshots should not become parents");
}

void test_rejects_ambiguous_remote_uuid() {
    std::vector<btrfsbackup::SnapshotInfo> remote{
        snapshot(
            btrfsbackup::SnapshotSide::Remote,
            "home",
            "home-2026-08-22T080000Z",
            "2026-08-22T080000Z",
            0,
            "/remote/home/a",
            true,
            "remote-a",
            "same-uuid"
        ),
        snapshot(
            btrfsbackup::SnapshotSide::Remote,
            "home",
            "home-2026-08-23T080000Z",
            "2026-08-23T080000Z",
            0,
            "/remote/home/b",
            true,
            "remote-b",
            "SAME-UUID"
        ),
    };

    test_helpers::expect_validation_error("ambiguous remote uuid", [&] {
        (void)btrfsbackup::select_incremental_parent("home", {}, remote, std::nullopt, false);
    }, "ambiguous remote parent received UUID for home");
}

} // namespace

int main() {
    test_selects_newest_uuid_matching_parent();
    test_skips_current_snapshot();
    test_missing_parent_rules();
    test_ignores_unusable_snapshots();
    test_rejects_ambiguous_remote_uuid();

    return test_helpers::finish("incremental parent tests");
}
