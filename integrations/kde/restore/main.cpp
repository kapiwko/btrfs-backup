// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RestoreController.hpp"

#include <KLocalizedQmlContext>

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>

int main(int argc, char** argv) {
    QGuiApplication application(argc, argv);
    application.setApplicationName(QStringLiteral("btrfs-backup-kde-restore"));
    application.setOrganizationDomain(QStringLiteral("io.github.btrfsbackup"));
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({QStringLiteral("url"), QStringLiteral("Backup URL to restore"), QStringLiteral("url")});
    parser.process(application);
    const QUrl source(parser.value(QStringLiteral("url")));
    btrfsbackup::kde::restore::RestoreController controller(source);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("restoreController"), &controller);
    KLocalization::setupLocalizedContext(&engine);
    engine.loadFromModule(QStringLiteral("org.btrfsbackup.restore"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty())
        return 1;
    return application.exec();
}
