// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "BackupReminderSettings.hpp"
#include "ManagerApi.hpp"

#include <QString>

namespace btrfsbackup::kde::monitor {

enum class TargetStorageNotificationLevel { none,
                                            warning,
                                            critical };

struct TargetStorageNotification {
    TargetStorageNotificationLevel level = TargetStorageNotificationLevel::none;
    int available_percent = -1;
    qint64 available_bytes = 0;
};

[[nodiscard]] TargetStorageNotification evaluate_target_storage(
    const ProfileSummary& profile,
    const TargetStatus& target,
    const BackupReminderConfiguration& configuration
);

} // namespace btrfsbackup::kde::monitor
