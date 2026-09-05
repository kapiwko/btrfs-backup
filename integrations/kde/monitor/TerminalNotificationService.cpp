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

QString friendly_error(const QString& error_code) {
    if (error_code == QStringLiteral("target.btrfs_uuid_mismatch"))
        return i18n("The connected device is not the configured backup target.");
    if (error_code == QStringLiteral("repository.recovery_required"))
        return i18n("The backup repository needs recovery before it can be used.");
    if (error_code.startsWith(QStringLiteral("transfer.")))
        return i18n("Backup data could not be transferred.");
    if (error_code.startsWith(QStringLiteral("hook.")))
        return i18n("A configured backup hook failed.");
    return i18n("The operation could not be completed.");
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
    const QString& operation_kind,
    const QString& terminal_state,
    const QString& error_code
) {
    if (profile_id.isEmpty() || run_id.isEmpty()) {
        return;
    }

    TerminalNotificationMessage message;
    message.profile_id = profile_id;
    const QString name = display_name(profile_id, profile_name);
    const bool target_validation = operation_kind == QStringLiteral("target-validation");
    if (terminal_state == QStringLiteral("succeeded")) {
        message.event_id = QStringLiteral("backupSucceeded");
        message.title = i18n("Backup completed");
        message.text = i18n("Backup “%1” completed successfully.", name);
    } else if (terminal_state == QStringLiteral("validated")) {
        message.event_id = QStringLiteral("targetValidated");
        message.title = i18n("Backup target checked");
        message.text = i18n("The backup target for “%1” passed validation.", name);
    } else if (terminal_state == QStringLiteral("skipped")) {
        message.event_id = QStringLiteral("backupSkipped");
        message.title = i18n("Backup skipped");
        message.text = i18n("Backup “%1” was already completed for this period.", name);
    } else if (terminal_state == QStringLiteral("failed")) {
        message.event_id = target_validation
            ? QStringLiteral("targetValidationFailed")
            : QStringLiteral("backupFailed");
        message.title = target_validation ? i18n("Backup target check failed") : i18n("Backup failed");
        message.text = friendly_error(error_code);
        message.error_code = error_code;
    } else if (terminal_state == QStringLiteral("cancelled")) {
        message.event_id = target_validation
            ? QStringLiteral("targetValidationCancelled")
            : QStringLiteral("backupCancelled");
        message.title = target_validation ? i18n("Backup target check cancelled") : i18n("Backup cancelled");
        message.text = target_validation
            ? i18n("The backup target check for “%1” was cancelled.", name)
            : i18n("Backup “%1” was cancelled.", name);
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
    QString text = message.text.toHtmlEscaped();
    if (!message.error_code.isEmpty()) {
        text += QStringLiteral("<br/><small>%1</small>").arg(
            i18n("Code: %1", message.error_code).toHtmlEscaped()
        );
    }
    notification->setText(text);
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
