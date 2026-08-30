// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TerminalNotificationService.hpp"

#include <KLocalizedString>
#include <KNotification>

#include <QProcess>

#include <utility>

namespace btrfsbackup::kde::monitor {

namespace {

constexpr auto notification_component = "btrfs-backup-kde-monitor";

QString display_name(const QString& profile_id, const QString& profile_name) {
    return profile_name.isEmpty() ? profile_id : profile_name;
}

} // namespace

TerminalNotificationService::TerminalNotificationService(QString state_path, Publisher publisher)
    : deduplicator_(std::move(state_path)),
      publisher_(publisher ? std::move(publisher) : &TerminalNotificationService::publish_to_desktop) {
}

void TerminalNotificationService::publish(
    const QString& profile_id,
    const QString& run_id,
    const QString& profile_name,
    const QString& terminal_state,
    const QString& error_code
) {
    if (profile_id.isEmpty() || run_id.isEmpty()) {
        return;
    }

    TerminalNotificationMessage message;
    message.profile_id = profile_id;
    const QString name = display_name(profile_id, profile_name);
    if (terminal_state == QStringLiteral("succeeded")) {
        message.event_id = QStringLiteral("backupSucceeded");
        message.title = i18n("Backup completed");
        message.text = i18n("Backup “%1” completed successfully.", name);
    } else if (terminal_state == QStringLiteral("failed")) {
        message.event_id = QStringLiteral("backupFailed");
        message.title = i18n("Backup failed");
        message.text = error_code.isEmpty()
            ? i18n("Backup “%1” failed.", name)
            : i18n("Backup “%1” failed (%2).", name, error_code);
    } else if (terminal_state == QStringLiteral("cancelled")) {
        message.event_id = QStringLiteral("backupCancelled");
        message.title = i18n("Backup cancelled");
        message.text = i18n("Backup “%1” was cancelled.", name);
    } else {
        return;
    }

    if (!deduplicator_.claim(profile_id, run_id, message.event_id)) {
        return;
    }
    publisher_(message);
}

void TerminalNotificationService::publish_to_desktop(const TerminalNotificationMessage& message) {
    auto* notification = new KNotification(message.event_id, KNotification::CloseOnTimeout);
    notification->setComponentName(QLatin1String(notification_component));
    notification->setTitle(message.title);
    notification->setText(message.text.toHtmlEscaped());
    notification->setIconName(QStringLiteral("drive-harddisk"));

    auto* details = notification->addAction(i18n("Show details"));
    QObject::connect(details, &KNotificationAction::activated, [profile_id = message.profile_id]() {
        QProcess::startDetached(QStringLiteral("systemsettings"), {
            QStringLiteral("kcm_btrfsbackup"),
            QStringLiteral("--args"),
            profile_id,
        });
    });
    auto* settings = notification->addAction(i18n("Configure notifications"));
    QObject::connect(settings, &KNotificationAction::activated, []() {
        QProcess::startDetached(QStringLiteral("systemsettings"), {QStringLiteral("kcm_notifications")});
    });
    notification->sendEvent();
}

} // namespace btrfsbackup::kde::monitor
