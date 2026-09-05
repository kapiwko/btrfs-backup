// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BackupProgressJob.hpp"

#include <KLocalizedString>
#include <KNotification>
#include <KUiServerV2JobTracker>

#include <QGuiApplication>
#include <QTimer>

using btrfsbackup::kde::monitor::BackupProgressJob;

namespace {

void show_transfer(KUiServerV2JobTracker& tracker) {
    auto* job = new BackupProgressJob(
        QStringLiteral("home"),
        QStringLiteral("readme-screenshot"),
        QStringLiteral("Home backup"),
        QStringLiteral("backup"),
        [](const QString&, const QString&) {}
    );
    tracker.registerJob(job);
    job->start();
    QTimer::singleShot(250, job, [job]() {
        job->update(
            68,
            94371840,
            true,
            i18n("Transferring backup data"),
            QStringLiteral("Documents"),
            QStringLiteral("Portable Backup")
        );
    });
}

void show_completion() {
    auto* notification = new KNotification(
        QStringLiteral("backupSucceeded"),
        KNotification::Persistent
    );
    notification->setComponentName(QStringLiteral("btrfs-backup-kde-monitor"));
    notification->setTitle(i18n("Backup completed"));
    notification->setText(i18n("Backup “Home backup” completed successfully."));
    notification->setIconName(QStringLiteral("drive-harddisk"));
    (void)notification->addAction(i18n("Show details"));
    (void)notification->addAction(i18n("Configure notifications"));
    notification->sendEvent();
}

} // namespace

int main(int argc, char* argv[]) {
    QGuiApplication::setApplicationName(QStringLiteral("btrfs-backup-kde-monitor"));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Btrfs Backup"));
    QGuiApplication::setDesktopFileName(QStringLiteral("io.github.btrfsbackup.ProgressMonitor"));
    QGuiApplication application(argc, argv);
    KLocalizedString::setApplicationDomain("btrfs-backup-kde-monitor");

    KUiServerV2JobTracker tracker;
    const QString mode = application.arguments().value(1, QStringLiteral("transfer"));
    if (mode == QStringLiteral("completion")) {
        QTimer::singleShot(250, &application, &show_completion);
    } else {
        show_transfer(tracker);
    }

    QTimer::singleShot(15000, &application, &QCoreApplication::quit);
    return application.exec();
}
