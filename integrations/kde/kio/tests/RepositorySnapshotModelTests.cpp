// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RepositorySnapshotModel.hpp"

#include <iostream>
#include <stdexcept>

namespace {

void expect(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

} // namespace

int main() {
    try {
        const auto snapshots = btrfsbackup::kde::kio::parse_repository_snapshots(QStringLiteral(R"({
            "schemaVersion": 1,
            "snapshots": [
                {"snapshotId":"new","profileId":"default","sourceId":"home","relativePath":"hosts/h/profiles/p/sources/home/new","createdAt":"2026-09-04T12:00:00Z","verified":true},
                {"snapshotId":"old","profileId":"default","sourceId":"home","relativePath":"hosts/h/profiles/p/sources/home/old","createdAt":"2026-09-03T12:00:00Z","verified":true},
                {"snapshotId":"other","profileId":"default","sourceId":"root","relativePath":"hosts/h/profiles/p/sources/root/other","createdAt":"2026-09-05T12:00:00Z","verified":true},
                {"snapshotId":"foreign","profileId":"archive","sourceId":"home","relativePath":"hosts/h/profiles/archive/sources/home/foreign","createdAt":"2026-09-07T12:00:00Z","verified":true},
                {"snapshotId":"unsafe","profileId":"default","sourceId":"home","relativePath":"hosts/h/profiles/p/sources/home/unsafe","createdAt":"2026-09-06T12:00:00Z","verified":false}
            ]
        })"));
        expect(snapshots.has_value(), "valid repository catalog was rejected");
        const auto versions = btrfsbackup::kde::kio::matching_versions(
            *snapshots,
            QStringLiteral("default"),
            QStringLiteral("home")
        );
        expect(versions.size() == 2, "profile, source or verification filtering failed");
        expect(versions[0].id == QStringLiteral("new") && versions[1].id == QStringLiteral("old"), "versions are not newest-first");
        expect(!btrfsbackup::kde::kio::parse_repository_snapshots(QStringLiteral(R"({"schemaVersion":1,"snapshots":[{"snapshotId":"same","profileId":"default","sourceId":"home","relativePath":"a","createdAt":"2026-09-04T12:00:00Z"},{"snapshotId":"same","profileId":"default","sourceId":"home","relativePath":"b","createdAt":"2026-09-03T12:00:00Z"}]})")).has_value(), "duplicate snapshot identifier was accepted");
        const auto page = btrfsbackup::kde::kio::parse_previous_versions_page(QStringLiteral(R"({
            "schemaVersion":1,
            "entries":[{"snapshotId":"new","createdAt":"2026-09-05T12:00:00Z","kind":"file","size":4,"mode":256,"modifiedAt":123}],
            "continuationToken":"next"
        })"));
        expect(page.has_value() && page->entries.size() == 1 && page->entries.front().snapshot_id == QStringLiteral("new") && page->continuation_token == QStringLiteral("next"), "valid previous versions page was rejected");
        expect(!btrfsbackup::kde::kio::parse_previous_versions_page(QStringLiteral(R"({"schemaVersion":1,"entries":[{"snapshotId":"new","createdAt":"bad","kind":"file","size":4,"mode":256,"modifiedAt":123}],"continuationToken":""})")).has_value(), "invalid previous versions page was accepted");
        expect(btrfsbackup::kde::kio::previous_versions_method_unavailable(QStringLiteral("org.freedesktop.DBus.Error.UnknownMethod")), "older daemon error did not enable the compatibility fallback");
        expect(!btrfsbackup::kde::kio::previous_versions_method_unavailable(QStringLiteral("io.github.btrfsbackup.Error.NotAuthorized")), "operational error incorrectly enabled the compatibility fallback");
        expect(
            btrfsbackup::kde::kio::previous_versions_stat_name(QStringLiteral("home/kamil/report.csv")) ==
                QStringLiteral("report.csv"),
            "nested previous-versions stat repeated the virtual namespace name"
        );
        expect(
            btrfsbackup::kde::kio::previous_versions_stat_name({}) == QStringLiteral(".versions"),
            "previous-versions root lost its virtual namespace name"
        );
        std::cout << "ok - repository snapshot model tests\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "not ok - " << error.what() << '\n';
        return 1;
    }
}
