// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RestoreJob.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>

#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {

void expect(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

void test_success_exposes_restore_statistics() {
    QTemporaryDir temporary;
    expect(temporary.isValid(), "could not create temporary directory");

    const QString source_path = temporary.filePath(QStringLiteral("source.txt"));
    QFile source(source_path);
    expect(source.open(QIODevice::WriteOnly), "could not create restore source");
    expect(source.write("restored payload") == 16, "could not write restore source");
    source.close();

    const std::filesystem::path root = temporary.path().toStdString();
    btrfsbackup::kde::restore::RestoreJob job({
        .transaction_id = "outcome-test",
        .snapshot_id = "snapshot",
        .snapshot_uuid = "uuid",
        .source = source_path.toStdString(),
        .destination = root / "restored.txt",
        .staging = root / ".btrfs-backup-restore-outcome-test.staging",
        .previous = root / ".btrfs-backup-restore-outcome-test.previous",
        .kind = btrfsbackup::restore::RestoreKind::Files,
        .existing_destination = btrfsbackup::restore::ExistingDestinationPolicy::Fail,
        .destination_exists = false,
    });

    QEventLoop loop;
    QObject::connect(&job, &KJob::result, &loop, &QEventLoop::quit);
    job.start();
    loop.exec();

    expect(job.error() == KJob::NoError, "restore job failed");
    expect(job.restoredFiles() == 1, "restore job did not retain the restored file count");
    expect(job.restoredBytes() == 16, "restore job did not retain the restored byte count");
    expect(QFile::exists(temporary.filePath(QStringLiteral("restored.txt"))), "restore destination is missing");
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    try {
        test_success_exposes_restore_statistics();
        std::cout << "ok - KDE restore job outcome tests\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "not ok - " << error.what() << '\n';
        return 1;
    }
}
