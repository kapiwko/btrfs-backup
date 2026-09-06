// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DesktopLauncher.hpp"
#include "ManagerApi.hpp"
#include "PreviousVersionsSelection.hpp"
#include "RepositorySnapshotModel.hpp"
#include "RestoreSource.hpp"

#include <iostream>
#include <stdexcept>

using Qt::StringLiterals::operator""_s;

namespace {

void expect(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

QString resolve_backup_coverage(const QString& local_path) {
    expect(
        local_path == u"/home/user/Documents/Quarterly report.txt"_s,
        "Dolphin did not resolve coverage for the selected local path"
    );
    return QStringLiteral(R"([
        {"profileId":"daily","sourceId":"home","relativePath":"Documents/Quarterly report.txt"},
        {"profileId":"archive","sourceId":"documents","relativePath":"Quarterly report.txt"}
    ])");
}

QString inspect_repository(const QString& profile_id) {
    expect(profile_id == u"archive"_s, "the selected coverage did not choose its repository");
    return QStringLiteral(R"({
        "schemaVersion": 1,
        "snapshots": [
            {"snapshotId":"archive-new","profileId":"archive","sourceId":"documents","relativePath":"hosts/desktop/profiles/archive/sources/documents/2026-09-05","createdAt":"2026-09-05T12:00:00Z","verified":true},
            {"snapshotId":"archive-old","profileId":"archive","sourceId":"documents","relativePath":"hosts/desktop/profiles/archive/sources/documents/2026-09-04","createdAt":"2026-09-04T12:00:00Z","verified":true},
            {"snapshotId":"unverified","profileId":"archive","sourceId":"documents","relativePath":"hosts/desktop/profiles/archive/sources/documents/2026-09-06","createdAt":"2026-09-06T12:00:00Z","verified":false},
            {"snapshotId":"other-source","profileId":"archive","sourceId":"home","relativePath":"hosts/desktop/profiles/archive/sources/home/2026-09-07","createdAt":"2026-09-07T12:00:00Z","verified":true}
        ]
    })");
}

} // namespace

int main() {
    try {
        const QUrl local_url = QUrl::fromLocalFile(u"/home/user/Documents/Quarterly report.txt"_s);
        expect(
            btrfsbackup::kde::dolphin::can_offer_previous_versions({local_url}, false),
            "Dolphin did not offer previous versions for a regular local file"
        );

        const auto coverage = btrfsbackup::kde::parse_backup_coverage(
            resolve_backup_coverage(local_url.toLocalFile())
        );
        expect(coverage && coverage->size() == 2, "ResolveBackupCoverageByFd response was not accepted");

        const auto versions_url = btrfsbackup::kde::dolphin::select_previous_versions_url(
            *coverage,
            1
        );
        expect(versions_url.has_value(), "Dolphin coverage selection did not produce a URL");
        expect(
            versions_url->path(QUrl::FullyDecoded) == u"/archive/.versions/documents/Quarterly report.txt"_s,
            "Dolphin opened the wrong .versions namespace"
        );

        const auto snapshots = btrfsbackup::kde::kio::parse_repository_snapshots(
            inspect_repository(coverage->at(1).profile_id)
        );
        expect(snapshots.has_value(), "KIO rejected the repository catalog");
        const auto versions = btrfsbackup::kde::kio::matching_versions(
            *snapshots,
            coverage->at(1).profile_id,
            coverage->at(1).source_id
        );
        expect(versions.size() == 2, "KIO exposed an unverified or unrelated version");
        expect(versions.front().id == u"archive-new"_s, "KIO did not list the newest version first");

        const auto restore_url = btrfsbackup::kde::kio::version_target_url(
            coverage->at(1).profile_id,
            versions.front().id,
            coverage->at(1).relative_path
        );
        expect(restore_url.has_value(), "KIO did not map the selected version to a snapshot URL");
        expect(
            restore_url->path(QUrl::FullyDecoded) == u"/archive/archive-new/Quarterly report.txt"_s,
            "KIO produced the wrong snapshot URL"
        );

        const auto restore_source = btrfsbackup::kde::restore::parse_restore_source(*restore_url);
        expect(restore_source.has_value(), "restore rejected the KIO snapshot URL");
        expect(restore_source->profile_id == u"archive"_s, "restore changed the profile identifier");
        expect(restore_source->snapshot_id == u"archive-new"_s, "restore changed the snapshot identifier");
        expect(
            restore_source->relative_path == u"Quarterly report.txt"_s,
            "restore changed the relative source path"
        );

        const auto launch = btrfsbackup::kde::launcher::open_restore_application(*restore_url);
        expect(
            launch.method == btrfsbackup::kde::launcher::LaunchMethod::Application,
            "the version action did not request an application launch"
        );
        expect(
            launch.desktop_name == u"io.github.btrfsbackup.Restore"_s,
            "the version action selected the wrong restore application"
        );
        expect(launch.urls == QList<QUrl>{*restore_url}, "the restore launch changed the snapshot URL");

        const QUrl reported_row_url(
            u"btrfsbackup:/default/home-2026-08-29T232016Z/kamil/"
            "_select_es_numer_skladowej_as_numer_es_nazwa_skladowej_as_nazwa__202507011505.csv"_s
        );
        const auto reported_source = btrfsbackup::kde::restore::parse_restore_source(reported_row_url);
        expect(reported_source.has_value(), "restore rejected the reported Dolphin row URL");
        expect(
            reported_source->snapshot_id == u"home-2026-08-29T232016Z"_s,
            "restore changed the reported snapshot identifier"
        );
        expect(
            reported_source->relative_path ==
                u"kamil/_select_es_numer_skladowej_as_numer_es_nazwa_skladowej_as_nazwa__202507011505.csv"_s,
            "restore changed the reported file path"
        );

        std::cout << "ok - local path to previous-version restore journey\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "not ok - " << error.what() << '\n';
        return 1;
    }
}
