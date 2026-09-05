// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BackupReminderPolicy.hpp"

namespace btrfsbackup::kde::monitor {

BackupReminder evaluate_backup_reminder(
    const ProfileSummary& profile,
    const RunStatus& status,
    const BackupReminderConfiguration& configuration,
    const QDateTime& now
) {
    if (!configuration.enabled || !profile.enabled || active_run_state(status.state))
        return {};

    const bool has_success = !status.last_success_at.isEmpty();
    const QString baseline_key = has_success ? status.last_success_at : status.last_attempt_at;
    const QDateTime baseline = QDateTime::fromString(baseline_key, Qt::ISODate);
    if (!baseline.isValid() || !now.isValid() || baseline > now)
        return {};

    constexpr qint64 seconds_per_day = 24 * 60 * 60;
    const int overdue_days = static_cast<int>(baseline.secsTo(now) / seconds_per_day);
    BackupReminderLevel level = BackupReminderLevel::none;
    if (overdue_days >= configuration.critical_days)
        level = BackupReminderLevel::critical;
    else if (overdue_days >= configuration.warning_days)
        level = BackupReminderLevel::warning;

    return {
        .level = level,
        .overdue_days = overdue_days,
        .baseline_key = baseline_key,
        .has_success = has_success,
    };
}

} // namespace btrfsbackup::kde::monitor
