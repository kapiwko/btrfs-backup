// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

#include <optional>

namespace btrfsbackup::kde {

struct TargetStorageStatus {
    qint64 capacity_bytes = 0;
    qint64 used_bytes = 0;
    qint64 available_bytes = 0;
    int usage_percent = -1;
    QString measured_at;
    bool live = false;
    QString space_state;
};

struct TargetStatus {
    QString profile_id;
    QString target_name;
    QString state;
    bool connected = false;
    bool unlocked = false;
    bool mounted = false;
    bool safe_to_remove = false;
    std::optional<TargetStorageStatus> storage;
};

[[nodiscard]] std::optional<TargetStatus> parse_target_status(const QString& payload);

} // namespace btrfsbackup::kde
