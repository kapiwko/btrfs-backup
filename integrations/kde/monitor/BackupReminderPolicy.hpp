// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "BackupReminderSettings.hpp"
#include "ManagerApi.hpp"

#include <QDateTime>
#include <QString>

namespace btrfsbackup::kde::monitor {

enum class BackupReminderLevel { none,
                                 warning,
                                 critical };

struct BackupReminder {
    BackupReminderLevel level = BackupReminderLevel::none;
    int overdue_days = 0;
    QString baseline_key;
    bool has_success = false;
};

[[nodiscard]] BackupReminder evaluate_backup_reminder(
    const ProfileSummary& profile,
    const RunStatus& status,
    const BackupReminderConfiguration& configuration,
    const QDateTime& now = QDateTime::currentDateTimeUtc()
);

} // namespace btrfsbackup::kde::monitor
