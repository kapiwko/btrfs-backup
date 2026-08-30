// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BackupProgressMonitor.hpp"

#include <KLocalizedString>
#include <KUiServerV2JobTracker>

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDBusConnection>

namespace {

constexpr auto session_service = "io.github.btrfsbackup.ProgressMonitor1";

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("btrfs-backup-kde-monitor"));
    QCoreApplication::setApplicationVersion(QStringLiteral(BTRFS_BACKUP_VERSION));
    KLocalizedString::setApplicationDomain("plasma_applet_org.btrfsbackup.plasmoid");

    QCommandLineParser parser;
    parser.setApplicationDescription(i18n("Publishes Btrfs backup progress to Plasma"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption bus_address_option(
        QStringLiteral("bus-address"),
        i18n("Use a custom manager D-Bus address"),
        QStringLiteral("address")
    );
    parser.addOption(bus_address_option);
    parser.process(application);

    QDBusConnection session_bus = QDBusConnection::sessionBus();
    if (!session_bus.isConnected() || !session_bus.registerService(QLatin1String(session_service))) {
        return 0;
    }

    const QString bus_address = parser.value(bus_address_option);
    const QDBusConnection manager_bus = bus_address.isEmpty()
        ? QDBusConnection::systemBus()
        : QDBusConnection::connectToBus(bus_address, QStringLiteral("btrfs-backup-kde-manager"));
    if (!manager_bus.isConnected()) {
        return 2;
    }

    KUiServerV2JobTracker tracker;
    BackupProgressMonitor monitor(manager_bus, tracker);
    monitor.start();
    return QCoreApplication::exec();
}
