// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <backup/model/IncrementalParent.hpp>

#include "support/ValidationTestHelpers.hpp"

namespace fs = std::filesystem;

namespace {

btrfsbackup::backup::SnapshotInfo snapshot(
    btrfsbackup::backup::SnapshotSide side,
    const std::string& source_id,
    const std::string& name,
    const std::string& timestamp,
    int sequence,
    const fs::path& path,
    bool readonly,
    const std::string& uuid,
    const std::string& received_uuid = ""
) {
    return btrfsbackup::backup::SnapshotInfo{
        .side = side,
        .source_id = btrfsbackup::SourceId{source_id},
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
    std::vector<btrfsbackup::backup::SnapshotInfo> local{
        snapshot(
            btrfsbackup::backup::SnapshotSide::Local,
            "home",
            "home-2026-08-22T080000Z",
            "2026-08-22T080000Z",
            0,
            "/local/home/home-2026-08-22T080000Z",
            true,
            "aaaaaaaa-aaaa-aaaa-aaaa-aaaaaaaaaaaa"
        ),
        snapshot(
            btrfsbackup::backup::SnapshotSide::Local,
            "home",
            "home-2026-08-23T080000Z",
            "2026-08-23T080000Z",
            0,
            "/local/home/home-2026-08-23T080000Z",
            true,
            "BBBBBBBB-BBBB-BBBB-BBBB-BBBBBBBBBBBB"
        ),
    };
    std::vector<btrfsbackup::backup::SnapshotInfo> remote{
        snapshot(
            btrfsbackup::backup::SnapshotSide::Remote,
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
            btrfsbackup::backup::SnapshotSide::Remote,
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

    btrfsbackup::backup::IncrementalParentSelection selection =
        btrfsbackup::backup::select_incremental_parent(btrfsbackup::SourceId{"home"}, local, remote, std::nullopt, true);

    const auto* incremental = std::get_if<btrfsbackup::backup::IncrementalTransfer>(&selection);
    test_helpers::expect_true("select incremental", incremental != nullptr, "expected incremental parent");
    test_helpers::expect_eq("select newest local", incremental->local_parent.path.string(), "/local/home/home-2026-08-23T080000Z");
    test_helpers::expect_eq("select matching remote", incremental->remote_parent.path.string(), "/remote/home/home-2026-08-23T080000Z");
}

void test_skips_current_snapshot() {
    std::vector<btrfsbackup::backup::SnapshotInfo> local{
        snapshot(
            btrfsbackup::backup::SnapshotSide::Local,
            "root",
            "root-2026-08-23T080000Z",
            "2026-08-23T080000Z",
            0,
            "/local/root/root-2026-08-23T080000Z",
            true,
            "current-uuid"
        ),
        snapshot(
            btrfsbackup::backup::SnapshotSide::Local,
            "root",
            "root-2026-08-22T080000Z",
            "2026-08-22T080000Z",
            0,
            "/local/root/root-2026-08-22T080000Z",
            true,
            "parent-uuid"
        ),
    };
    std::vector<btrfsbackup::backup::SnapshotInfo> remote{
        snapshot(
            btrfsbackup::backup::SnapshotSide::Remote,
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
            btrfsbackup::backup::SnapshotSide::Remote,
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

    btrfsbackup::backup::IncrementalParentSelection selection =
        btrfsbackup::backup::select_incremental_parent(btrfsbackup::SourceId{"root"}, local, remote, fs::path("/local/root/root-2026-08-23T080000Z"), true);

    const auto* incremental = std::get_if<btrfsbackup::backup::IncrementalTransfer>(&selection);
    test_helpers::expect_true("skip current incremental", incremental != nullptr, "expected older parent");
    test_helpers::expect_eq("skip current local", incremental->local_parent.path.string(), "/local/root/root-2026-08-22T080000Z");
}

void test_missing_parent_rules() {
    std::vector<btrfsbackup::backup::SnapshotInfo> local{
        snapshot(
            btrfsbackup::backup::SnapshotSide::Local,
            "home",
            "home-2026-08-23T080000Z",
            "2026-08-23T080000Z",
            0,
            "/local/home/home-2026-08-23T080000Z",
            true,
            "local-only"
        ),
    };
    std::vector<btrfsbackup::backup::SnapshotInfo> remote{
        snapshot(
            btrfsbackup::backup::SnapshotSide::Remote,
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

    btrfsbackup::backup::IncrementalParentSelection full =
        btrfsbackup::backup::select_incremental_parent(btrfsbackup::SourceId{"home"}, local, remote, std::nullopt, false);
    test_helpers::expect_true(
        "missing parent full",
        std::holds_alternative<btrfsbackup::backup::FullTransfer>(full),
        "optional incremental should fall back to full"
    );

    test_helpers::expect_validation_error("missing required parent", [&] { (void)btrfsbackup::backup::select_incremental_parent(btrfsbackup::SourceId{"home"}, local, remote, std::nullopt, true); }, "Remote snapshots exist for home, but no UUID-matching local parent was found.");
}

void test_ignores_unusable_snapshots() {
    std::vector<btrfsbackup::backup::SnapshotInfo> local{
        snapshot(
            btrfsbackup::backup::SnapshotSide::Local,
            "home",
            "home-2026-08-23T080000Z",
            "2026-08-23T080000Z",
            0,
            "/local/home/not-readonly",
            false,
            "matched"
        ),
        snapshot(
            btrfsbackup::backup::SnapshotSide::Local,
            "home",
            "home-2026-08-22T080000Z",
            "2026-08-22T080000Z",
            0,
            "/local/home/no-uuid",
            true,
            ""
        ),
    };
    std::vector<btrfsbackup::backup::SnapshotInfo> remote{
        snapshot(
            btrfsbackup::backup::SnapshotSide::Remote,
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

    btrfsbackup::backup::IncrementalParentSelection selection =
        btrfsbackup::backup::select_incremental_parent(btrfsbackup::SourceId{"home"}, local, remote, std::nullopt, false);
    test_helpers::expect_true(
        "ignore unusable",
        std::holds_alternative<btrfsbackup::backup::FullTransfer>(selection),
        "unusable snapshots should not become parents"
    );
}

void test_rejects_ambiguous_remote_uuid() {
    std::vector<btrfsbackup::backup::SnapshotInfo> remote{
        snapshot(
            btrfsbackup::backup::SnapshotSide::Remote,
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
            btrfsbackup::backup::SnapshotSide::Remote,
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

    test_helpers::expect_validation_error("ambiguous remote uuid", [&] { (void)btrfsbackup::backup::select_incremental_parent(btrfsbackup::SourceId{"home"}, {}, remote, std::nullopt, false); }, "ambiguous remote parent received UUID for home");
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
