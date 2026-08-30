// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include <filesystem>
#include <optional>
#include <string>

#include <restore/RepositoryDiscoveryService.hpp>
#include <restore/RestoreError.hpp>
#include <restore/SnapshotBrowser.hpp>
#include <support/TestHelpers.hpp>

namespace fs = std::filesystem;

namespace {

void write_repository(const fs::path& root) {
    test_helpers::write_file(root / "repository.json", R"({
        "schemaVersion": 1,
        "repositoryId": "repo-1",
        "targetFilesystemUuid": "target-uuid",
        "createdAt": "2026-08-30T120000Z",
        "features": ["catalog-v1"]
    })");
    test_helpers::write_file(root / "catalog.json", R"({
        "schemaVersion": 1,
        "generation": 7,
        "snapshots": [
            {
                "snapshotId": "snap-new",
                "hostId": "host-1",
                "profileId": "default",
                "sourceId": "home",
                "relativePath": "hosts/host-1/profiles/default/sources/home/home-2026-08-30T120000Z",
                "createdAt": "2026-08-30T120000Z",
                "uuid": "new-uuid",
                "receivedUuid": "new-received",
                "verified": true
            },
            {
                "snapshotId": "snap-old",
                "hostId": "host-1",
                "profileId": "default",
                "sourceId": "home",
                "relativePath": "hosts/host-1/profiles/default/sources/home/home-2026-08-29T120000Z",
                "createdAt": "2026-08-29T120000Z",
                "uuid": "old-uuid",
                "receivedUuid": "old-received",
                "verified": true
            }
        ]
    })");
}

void test_discovery_browse_and_versions() {
    const fs::path root = test_helpers::test_root("restore-catalog", "valid");
    write_repository(root);
    const fs::path new_snapshot = root / "hosts/host-1/profiles/default/sources/home/home-2026-08-30T120000Z";
    const fs::path old_snapshot = root / "hosts/host-1/profiles/default/sources/home/home-2026-08-29T120000Z";
    test_helpers::write_file(new_snapshot / "Documents/report.txt", "new");
    test_helpers::write_file(old_snapshot / "Documents/report.txt", "old");
    test_helpers::write_file(new_snapshot / "Documents/other.txt", "other");

    btrfsbackup::restore::RepositoryDiscoveryService discovery([&](const fs::path& path) {
        const bool is_new = path == new_snapshot;
        return std::optional<btrfsbackup::restore::DiscoveredSnapshotMetadata>{
            btrfsbackup::restore::DiscoveredSnapshotMetadata{
                .is_subvolume = true,
                .readonly = true,
                .uuid = is_new ? "new-uuid" : "old-uuid",
                .received_uuid = is_new ? "new-received" : "old-received",
            }
        };
    });
    const btrfsbackup::restore::RepositoryCatalog catalog = discovery.discover(root);
    test_helpers::expect_eq("repository id", catalog.identity().repository_id, "repo-1");
    test_helpers::expect_eq("catalog generation", std::to_string(catalog.generation()), "7");
    test_helpers::expect_eq("newest first", catalog.snapshots().at(0).snapshot_id, "snap-new");

    btrfsbackup::restore::SnapshotBrowser browser;
    const auto entries = browser.list(catalog, "snap-new", btrfsbackup::restore::RelativeRestorePath{"Documents"});
    test_helpers::expect_eq("browse count", std::to_string(entries.size()), "2");
    test_helpers::expect_eq("browse sorted", entries.at(0).name, "other.txt");
    const auto versions = btrfsbackup::restore::find_versions(
        catalog,
        "host-1",
        "default",
        "home",
        btrfsbackup::restore::RelativeRestorePath{"Documents/report.txt"}
    );
    test_helpers::expect_eq("version count", std::to_string(versions.size()), "2");
    test_helpers::expect_eq("newest version", versions.at(0)->snapshot_id, "snap-new");
}

void test_rejects_identity_mismatch() {
    const fs::path root = test_helpers::test_root("restore-catalog", "identity");
    write_repository(root);
    fs::create_directories(root / "hosts/host-1/profiles/default/sources/home/home-2026-08-30T120000Z");
    fs::create_directories(root / "hosts/host-1/profiles/default/sources/home/home-2026-08-29T120000Z");
    btrfsbackup::restore::RepositoryDiscoveryService discovery([](const fs::path&) {
        return std::optional<btrfsbackup::restore::DiscoveredSnapshotMetadata>{
            btrfsbackup::restore::DiscoveredSnapshotMetadata{true, true, "wrong", "wrong"}
        };
    });
    try {
        (void)discovery.discover(root);
        test_helpers::fail("identity mismatch", "discovery accepted mismatched Btrfs identity");
    } catch (const btrfsbackup::restore::RestoreError& error) {
        test_helpers::expect_eq(
            "identity error code",
            btrfsbackup::restore::restore_error_code_name(error.code()),
            "snapshot-identity-mismatch"
        );
    }
}

} // namespace

int main() {
    test_discovery_browse_and_versions();
    test_rejects_identity_mismatch();
    return test_helpers::finish("restore repository catalog tests passed");
}
