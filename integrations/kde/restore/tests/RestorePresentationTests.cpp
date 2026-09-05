// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RepositoryCatalogDecoder.hpp"
#include "RestoreErrorPresentation.hpp"

#include <QCoreApplication>

#include <iostream>
#include <stdexcept>

namespace {

void expect(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

template <typename Function>
void expect_restore_error(
    btrfsbackup::restore::RestoreErrorCode expected,
    Function&& function,
    const char* message
) {
    try {
        function();
    } catch (const btrfsbackup::restore::RestoreError& error) {
        expect(error.code() == expected, message);
        return;
    }
    throw std::runtime_error(message);
}

QString valid_catalog() {
    return QStringLiteral(R"({
        "schemaVersion": 1,
        "repositoryId": "repository-1",
        "targetFilesystemUuid": "filesystem-1",
        "createdAt": "2026-09-05T12:00:00Z",
        "features": ["catalog-v1"],
        "generation": 7,
        "snapshots": [{
            "snapshotId": "snapshot-1",
            "hostId": "host-1",
            "profileId": "home",
            "sourceId": "documents",
            "relativePath": "hosts/host-1/profiles/home/sources/documents/snapshot-1",
            "createdAt": "2026-09-05T11:00:00Z",
            "uuid": "uuid-1",
            "receivedUuid": "received-1",
            "parentUuid": "parent-1",
            "verified": true
        }]
    })");
}

void test_valid_catalog() {
    const auto catalog = btrfsbackup::kde::restore::RepositoryCatalogDecoder{}.decode(
        valid_catalog(),
        "/proc/self/fd/42"
    );
    expect(catalog.root() == "/proc/self/fd/42", "catalog root was not preserved");
    expect(catalog.identity().repository_id == "repository-1", "repository identity was not decoded");
    expect(catalog.generation() == 7, "repository generation was not decoded");
    expect(catalog.snapshots().size() == 1, "snapshot was not decoded");
    expect(catalog.snapshots().front().snapshot_id == "snapshot-1", "snapshot identity was not decoded");
}

void test_invalid_catalogs() {
    const btrfsbackup::kde::restore::RepositoryCatalogDecoder decoder;
    expect_restore_error(
        btrfsbackup::restore::RestoreErrorCode::CatalogInvalid,
        [&] { (void)decoder.decode(QStringLiteral("{"), "/tmp/root"); },
        "malformed JSON did not produce catalog-invalid"
    );
    expect_restore_error(
        btrfsbackup::restore::RestoreErrorCode::RepositoryFormatUnsupported,
        [&] {
            QString payload = valid_catalog();
            payload.replace(QStringLiteral("\"schemaVersion\": 1"), QStringLiteral("\"schemaVersion\": 2"));
            (void)decoder.decode(payload, "/tmp/root");
        },
        "unsupported schema did not produce repository-format-unsupported"
    );
    expect_restore_error(
        btrfsbackup::restore::RestoreErrorCode::CatalogInvalid,
        [&] {
            QString payload = valid_catalog();
            payload.replace(QStringLiteral("\"snapshotId\": \"snapshot-1\","), QString{});
            (void)decoder.decode(payload, "/tmp/root");
        },
        "missing snapshot id did not produce catalog-invalid"
    );
    expect_restore_error(
        btrfsbackup::restore::RestoreErrorCode::CatalogInvalid,
        [&] {
            QString payload = valid_catalog();
            payload.replace(QStringLiteral("\"verified\": true"), QStringLiteral("\"verified\": false"));
            (void)decoder.decode(payload, "/tmp/root");
        },
        "unverified snapshot did not produce catalog-invalid"
    );
    expect_restore_error(
        btrfsbackup::restore::RestoreErrorCode::CatalogInvalid,
        [&] {
            QString payload = valid_catalog();
            payload.replace(QStringLiteral("2026-09-05T11:00:00Z"), QStringLiteral("not-a-timestamp"));
            (void)decoder.decode(payload, "/tmp/root");
        },
        "invalid timestamp did not produce catalog-invalid"
    );
    expect_restore_error(
        btrfsbackup::restore::RestoreErrorCode::PathTraversal,
        [&] {
            QString payload = valid_catalog();
            payload.replace(
                QStringLiteral("hosts/host-1/profiles/home/sources/documents/snapshot-1"),
                QStringLiteral("../outside")
            );
            (void)decoder.decode(payload, "/tmp/root");
        },
        "traversing repository path was accepted"
    );
}

void test_error_presentation() {
    const auto known = btrfsbackup::kde::restore::present_restore_error(
        btrfsbackup::restore::RestoreErrorCode::SnapshotNotFound,
        QStringLiteral("snapshot id abc was not found")
    );
    expect(known.code == QStringLiteral("restore.snapshot-not-found"), "stable restore error code is wrong");
    expect(!known.message.isEmpty(), "friendly restore error message is empty");
    expect(known.message != known.technical_details, "technical details leaked into the primary message");
    expect(known.technical_details == QStringLiteral("snapshot id abc was not found"), "technical details changed");

    const btrfsbackup::restore::RestoreErrorCode codes[]{
        btrfsbackup::restore::RestoreErrorCode::RepositoryNotFound,
        btrfsbackup::restore::RestoreErrorCode::RepositoryMetadataInvalid,
        btrfsbackup::restore::RestoreErrorCode::RepositoryFormatUnsupported,
        btrfsbackup::restore::RestoreErrorCode::CatalogInvalid,
        btrfsbackup::restore::RestoreErrorCode::SnapshotNotFound,
        btrfsbackup::restore::RestoreErrorCode::SnapshotIdentityMismatch,
        btrfsbackup::restore::RestoreErrorCode::PathInvalid,
        btrfsbackup::restore::RestoreErrorCode::PathTraversal,
        btrfsbackup::restore::RestoreErrorCode::SymlinkRejected,
        btrfsbackup::restore::RestoreErrorCode::MountBoundaryRejected,
        btrfsbackup::restore::RestoreErrorCode::DestinationExists,
        btrfsbackup::restore::RestoreErrorCode::DestinationUnsafe,
        btrfsbackup::restore::RestoreErrorCode::InsufficientSpace,
        btrfsbackup::restore::RestoreErrorCode::Cancelled,
        btrfsbackup::restore::RestoreErrorCode::CopyFailed,
        btrfsbackup::restore::RestoreErrorCode::VerificationFailed,
        btrfsbackup::restore::RestoreErrorCode::CleanupIncomplete,
        btrfsbackup::restore::RestoreErrorCode::RollbackIncomplete,
    };
    for (const auto code : codes) {
        const auto presentation = btrfsbackup::kde::restore::present_restore_error(
            code,
            QStringLiteral("technical detail")
        );
        expect(!presentation.message.isEmpty(), "restore error has no friendly message");
        expect(presentation.code.startsWith(QStringLiteral("restore.")), "restore error has no stable code namespace");
        expect(presentation.technical_details == QStringLiteral("technical detail"), "restore error details changed");
    }

    const auto unexpected = btrfsbackup::kde::restore::present_unexpected_restore_error(
        QStringLiteral("transport disconnected")
    );
    expect(unexpected.code == QStringLiteral("restore.unexpected"), "unexpected error code is unstable");
    expect(unexpected.message != unexpected.technical_details, "unexpected details leaked into the primary message");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    try {
        test_valid_catalog();
        test_invalid_catalogs();
        test_error_presentation();
        std::cout << "ok - KDE restore decoder and error presentation tests\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "not ok - " << error.what() << '\n';
        return 1;
    }
}
