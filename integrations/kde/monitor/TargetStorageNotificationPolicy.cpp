// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TargetStorageNotificationPolicy.hpp"

namespace btrfsbackup::kde::monitor {

TargetStorageNotification evaluate_target_storage(
    const ProfileSummary& profile,
    const TargetStatus& target,
    const BackupReminderConfiguration& configuration
) {
    if (!configuration.storage_enabled || !profile.enabled || !target.storage.has_value())
        return {};

    const TargetStorageStatus& storage = *target.storage;
    const int available_percent = 100 - storage.usage_percent;
    TargetStorageNotificationLevel level = TargetStorageNotificationLevel::none;
    if (storage.space_state == QStringLiteral("below-configured-minimum") ||
        available_percent <= configuration.storage_critical_percent) {
        level = TargetStorageNotificationLevel::critical;
    } else if (available_percent <= configuration.storage_warning_percent) {
        level = TargetStorageNotificationLevel::warning;
    }
    return {
        .level = level,
        .available_percent = available_percent,
        .available_bytes = storage.available_bytes,
    };
}

} // namespace btrfsbackup::kde::monitor
