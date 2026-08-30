// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TerminalNotificationService.hpp"

#include <KLocalizedString>

#include <QCoreApplication>
#include <QTemporaryDir>

#include <iostream>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "not ok - " << message << '\n';
        ++failures;
    }
}

void test_terminal_notifications_are_persistent_and_deduplicated() {
    QTemporaryDir directory;
    const QString state_path = directory.filePath(QStringLiteral("notifications.json"));
    std::vector<TerminalNotificationMessage> published;
    const auto publisher = [&](const TerminalNotificationMessage& message) {
        published.push_back(message);
    };

    {
        TerminalNotificationService notifications(state_path, publisher);
        notifications.publish(
            QStringLiteral("default"),
            QStringLiteral("run-1"),
            QStringLiteral("Home"),
            QStringLiteral("failed"),
            QStringLiteral("target.full")
        );
        notifications.publish(
            QStringLiteral("default"),
            QStringLiteral("run-1"),
            QStringLiteral("Home"),
            QStringLiteral("failed"),
            QStringLiteral("target.full")
        );
    }

    expect(published.size() == 1, "terminal event is published at most once per monitor process");
    expect(
        published.front().event_id == QStringLiteral("backupFailed") &&
            published.front().text.contains(QStringLiteral("target.full")),
        "failure notification includes the stable public error code"
    );

    TerminalNotificationService restarted(state_path, publisher);
    restarted.publish(
        QStringLiteral("default"),
        QStringLiteral("run-1"),
        QStringLiteral("Home"),
        QStringLiteral("failed"),
        QStringLiteral("target.full")
    );
    expect(published.size() == 1, "monitor restart does not repeat a handled terminal event");

    restarted.publish(
        QStringLiteral("default"),
        QStringLiteral("run-2"),
        QStringLiteral("Home"),
        QStringLiteral("succeeded"),
        QStringLiteral("private details must not be used")
    );
    expect(
        published.size() == 2 && published.back().event_id == QStringLiteral("backupSucceeded") &&
            !published.back().text.contains(QStringLiteral("private")),
        "success notification ignores diagnostic text"
    );
}

void test_non_terminal_status_is_ignored() {
    QTemporaryDir directory;
    int published = 0;
    TerminalNotificationService notifications(
        directory.filePath(QStringLiteral("notifications.json")),
        [&](const TerminalNotificationMessage&) {
            ++published;
        }
    );
    notifications.publish(
        QStringLiteral("default"),
        QStringLiteral("run-1"),
        QStringLiteral("Home"),
        QStringLiteral("running"),
        {}
    );
    expect(published == 0, "active status does not emit a terminal notification");
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    KLocalizedString::setApplicationDomain("plasma_applet_org.btrfsbackup.plasmoid");
    test_terminal_notifications_are_persistent_and_deduplicated();
    test_non_terminal_status_is_ignored();
    if (failures == 0) {
        std::cout << "ok - KDE terminal notification tests\n";
    }
    return failures == 0 ? 0 : 1;
}
