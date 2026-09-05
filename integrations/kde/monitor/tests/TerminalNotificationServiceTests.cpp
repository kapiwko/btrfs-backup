// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TerminalNotificationService.hpp"
#include "BackupReminderPolicy.hpp"
#include "TargetStorageNotificationPolicy.hpp"

#include <KLocalizedString>

#include <QCoreApplication>
#include <QTemporaryDir>

#include <iostream>
#include <vector>

namespace {

using btrfsbackup::kde::monitor::TerminalNotificationMessage;
using btrfsbackup::kde::monitor::TerminalNotificationService;
using btrfsbackup::kde::monitor::BackupReminderLevel;
using btrfsbackup::kde::monitor::evaluate_backup_reminder;
using btrfsbackup::kde::monitor::evaluate_target_storage;
using btrfsbackup::kde::monitor::TargetStorageNotificationLevel;

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
            QStringLiteral("backup"),
            QStringLiteral("failed"),
            QStringLiteral("target.full")
        );
        notifications.publish(
            QStringLiteral("default"),
            QStringLiteral("run-1"),
            QStringLiteral("Home"),
            QStringLiteral("backup"),
            QStringLiteral("failed"),
            QStringLiteral("target.full")
        );
    }

    expect(published.size() == 1, "terminal event is published at most once per monitor process");
    expect(
        published.front().event_id == QStringLiteral("backupFailed") &&
            published.front().text == QStringLiteral("The operation could not be completed.") &&
            published.front().error_code == QStringLiteral("target.full"),
        "failure notification separates the friendly message from the stable error code"
    );

    TerminalNotificationService restarted(state_path, publisher);
    restarted.publish(
        QStringLiteral("default"),
        QStringLiteral("run-1"),
        QStringLiteral("Home"),
        QStringLiteral("backup"),
        QStringLiteral("failed"),
        QStringLiteral("target.full")
    );
    expect(published.size() == 1, "monitor restart does not repeat a handled terminal event");

    restarted.publish(
        QStringLiteral("default"),
        QStringLiteral("run-2"),
        QStringLiteral("Home"),
        QStringLiteral("backup"),
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
        QStringLiteral("backup"),
        QStringLiteral("running"),
        {}
    );
    expect(published == 0, "active status does not emit a terminal notification");
}

void test_operation_specific_terminal_states_are_distinct() {
    QTemporaryDir directory;
    std::vector<TerminalNotificationMessage> published;
    TerminalNotificationService notifications(
        directory.filePath(QStringLiteral("notifications.json")),
        [&](const TerminalNotificationMessage& message) { published.push_back(message); }
    );

    notifications.publish(
        QStringLiteral("default"),
        QStringLiteral("validation-1"),
        QStringLiteral("Home"),
        QStringLiteral("target-validation"),
        QStringLiteral("validated"),
        {}
    );
    notifications.publish(
        QStringLiteral("default"),
        QStringLiteral("backup-1"),
        QStringLiteral("Home"),
        QStringLiteral("backup"),
        QStringLiteral("skipped"),
        {}
    );
    notifications.publish(
        QStringLiteral("default"),
        QStringLiteral("validation-2"),
        QStringLiteral("Home"),
        QStringLiteral("target-validation"),
        QStringLiteral("cancelled"),
        QStringLiteral("backup.cancelled")
    );

    expect(published.size() == 3, "validated, skipped, and cancelled statuses all publish notifications");
    expect(published[0].event_id == QStringLiteral("targetValidated"), "validated target is not a backup success");
    expect(published[1].event_id == QStringLiteral("backupSkipped"), "skipped backup is not a backup success");
    expect(
        published[2].event_id == QStringLiteral("targetValidationCancelled"),
        "cancelled validation is identified separately"
    );
}

void test_backup_reminder_thresholds() {
    const btrfsbackup::kde::ProfileSummary profile{
        .id = QStringLiteral("default"),
        .name = QStringLiteral("Home"),
        .enabled = true,
        .target_name = QStringLiteral("Backup disk"),
        .sources = {},
        .configuration_valid = true,
        .configuration_error_code = {},
    };
    btrfsbackup::kde::RunStatus status;
    status.state = QStringLiteral("idle");
    status.last_success_at = QStringLiteral("2026-08-01T12:00:00Z");
    const btrfsbackup::kde::BackupReminderConfiguration configuration{
        .enabled = true,
        .warning_days = 7,
        .critical_days = 14,
        .storage_enabled = true,
        .storage_warning_percent = 15,
        .storage_critical_percent = 5,
    };

    expect(
        evaluate_backup_reminder(
            profile,
            status,
            configuration,
            QDateTime::fromString(QStringLiteral("2026-08-08T11:59:59Z"), Qt::ISODate)
        )
                .level == BackupReminderLevel::none,
        "warning starts only after the configured number of full days"
    );
    const auto warning = evaluate_backup_reminder(
        profile,
        status,
        configuration,
        QDateTime::fromString(QStringLiteral("2026-08-09T12:00:00Z"), Qt::ISODate)
    );
    expect(
        warning.level == BackupReminderLevel::warning && warning.overdue_days == 8 && warning.has_success,
        "warning reports elapsed days since the last successful backup"
    );
    expect(
        evaluate_backup_reminder(
            profile,
            status,
            configuration,
            QDateTime::fromString(QStringLiteral("2026-08-15T12:00:00Z"), Qt::ISODate)
        )
                .level == BackupReminderLevel::critical,
        "critical threshold supersedes the warning threshold"
    );

    status.state = QStringLiteral("running");
    expect(
        evaluate_backup_reminder(
            profile,
            status,
            configuration,
            QDateTime::fromString(QStringLiteral("2026-09-01T12:00:00Z"), Qt::ISODate)
        )
                .level == BackupReminderLevel::none,
        "an active backup suppresses overdue reminders"
    );
}

void test_target_storage_thresholds() {
    const btrfsbackup::kde::ProfileSummary profile{
        .id = QStringLiteral("default"),
        .name = QStringLiteral("Home"),
        .enabled = true,
        .target_name = QStringLiteral("Backup disk"),
        .sources = {},
        .configuration_valid = true,
        .configuration_error_code = {},
    };
    btrfsbackup::kde::TargetStatus target;
    target.profile_id = profile.id;
    target.storage = btrfsbackup::kde::TargetStorageStatus{
        .capacity_bytes = 1000,
        .used_bytes = 840,
        .available_bytes = 160,
        .usage_percent = 84,
        .measured_at = QStringLiteral("2026-09-05T12:00:00Z"),
        .live = true,
        .space_state = QStringLiteral("normal"),
    };
    const btrfsbackup::kde::BackupReminderConfiguration configuration{
        .enabled = true,
        .warning_days = 7,
        .critical_days = 14,
        .storage_enabled = true,
        .storage_warning_percent = 15,
        .storage_critical_percent = 5,
    };

    expect(
        evaluate_target_storage(profile, target, configuration).level == TargetStorageNotificationLevel::none,
        "storage above the warning threshold is normal"
    );
    target.storage->usage_percent = 85;
    expect(
        evaluate_target_storage(profile, target, configuration).level == TargetStorageNotificationLevel::warning,
        "storage warning starts at the configured free percentage"
    );
    target.storage->usage_percent = 95;
    expect(
        evaluate_target_storage(profile, target, configuration).level == TargetStorageNotificationLevel::critical,
        "critical free percentage supersedes the warning"
    );
    target.storage->usage_percent = 50;
    target.storage->space_state = QStringLiteral("below-configured-minimum");
    expect(
        evaluate_target_storage(profile, target, configuration).level == TargetStorageNotificationLevel::critical,
        "the profile minimum free space is always critical"
    );
}

void test_target_storage_notifications_follow_transitions() {
    QTemporaryDir directory;
    std::vector<TerminalNotificationMessage> published;
    TerminalNotificationService notifications(
        directory.filePath(QStringLiteral("notifications.json")),
        [&](const TerminalNotificationMessage& message) { published.push_back(message); }
    );
    const btrfsbackup::kde::monitor::TargetStorageNotification warning{
        .level = TargetStorageNotificationLevel::warning,
        .available_percent = 15,
        .available_bytes = 150,
    };
    notifications.publish_target_storage(
        QStringLiteral("default"),
        QStringLiteral("Home"),
        QStringLiteral("Backup disk"),
        warning
    );
    notifications.publish_target_storage(
        QStringLiteral("default"),
        QStringLiteral("Home"),
        QStringLiteral("Backup disk"),
        warning
    );
    auto critical = warning;
    critical.level = TargetStorageNotificationLevel::critical;
    critical.available_percent = 5;
    notifications.publish_target_storage(
        QStringLiteral("default"),
        QStringLiteral("Home"),
        QStringLiteral("Backup disk"),
        critical
    );
    notifications.publish_target_storage(
        QStringLiteral("default"),
        QStringLiteral("Home"),
        QStringLiteral("Backup disk"),
        {}
    );
    notifications.publish_target_storage(
        QStringLiteral("default"),
        QStringLiteral("Home"),
        QStringLiteral("Backup disk"),
        warning
    );

    expect(published.size() == 3, "storage notifications are repeated only after recovery");
    expect(
        published[0].event_id == QStringLiteral("targetSpaceLow") &&
            published[1].event_id == QStringLiteral("targetSpaceCritical") &&
            published[2].event_id == QStringLiteral("targetSpaceLow"),
        "storage transitions preserve warning and critical severity"
    );
}

void test_backup_reminders_are_deduplicated_per_threshold() {
    QTemporaryDir directory;
    std::vector<TerminalNotificationMessage> published;
    TerminalNotificationService notifications(
        directory.filePath(QStringLiteral("notifications.json")),
        [&](const TerminalNotificationMessage& message) { published.push_back(message); }
    );
    const btrfsbackup::kde::monitor::BackupReminder warning{
        .level = BackupReminderLevel::warning,
        .overdue_days = 7,
        .baseline_key = QStringLiteral("2026-08-01T12:00:00Z"),
        .has_success = true,
    };
    notifications.publish_backup_reminder(
        QStringLiteral("default"),
        QStringLiteral("Home"),
        QStringLiteral("Backup disk"),
        warning
    );
    notifications.publish_backup_reminder(
        QStringLiteral("default"),
        QStringLiteral("Home"),
        QStringLiteral("Backup disk"),
        warning
    );
    auto critical = warning;
    critical.level = BackupReminderLevel::critical;
    critical.overdue_days = 14;
    notifications.publish_backup_reminder(
        QStringLiteral("default"),
        QStringLiteral("Home"),
        QStringLiteral("Backup disk"),
        critical
    );

    expect(published.size() == 2, "warning and critical reminders are each published once");
    expect(
        published[0].event_id == QStringLiteral("backupOverdueWarning") &&
            published[1].event_id == QStringLiteral("backupOverdueCritical"),
        "reminder thresholds use distinct desktop notification events"
    );
}

} // namespace

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    KLocalizedString::setApplicationDomain("plasma_applet_org.btrfsbackup.plasmoid");
    test_terminal_notifications_are_persistent_and_deduplicated();
    test_non_terminal_status_is_ignored();
    test_operation_specific_terminal_states_are_distinct();
    test_backup_reminder_thresholds();
    test_backup_reminders_are_deduplicated_per_threshold();
    test_target_storage_thresholds();
    test_target_storage_notifications_follow_transitions();
    if (failures == 0) {
        std::cout << "ok - KDE terminal notification tests\n";
    }
    return failures == 0 ? 0 : 1;
}
