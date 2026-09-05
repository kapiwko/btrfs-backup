// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RestoreController.hpp"

#include <KLocalizedString>
#include <KLocalizedQmlContext>

#include <QCommandLineParser>
#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QIcon>
#include <QVariant>

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("btrfs-backup-kde-restore"));
    application.setOrganizationDomain(QStringLiteral("io.github.btrfsbackup"));
    application.setDesktopFileName(QStringLiteral("io.github.btrfsbackup.Restore"));
    application.setWindowIcon(QIcon::fromTheme(QStringLiteral("document-revert")));
    KLocalizedString::setApplicationDomain("btrfs-backup-kde-restore");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringLiteral("url"), QStringLiteral("Backup URL to restore"), QStringLiteral("url")});
    parser.process(application);
    const QUrl source(parser.value(QStringLiteral("url")));
    btrfsbackup::kde::restore::RestoreController controller(source);
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
    KLocalization::setupLocalizedContext(&engine);
    engine.loadFromModule(QStringLiteral("org.btrfsbackup.restore"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty())
        return 1;
    return application.exec();
}
