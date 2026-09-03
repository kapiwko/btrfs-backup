// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BackupHistoryModel.hpp"

#include <ManagerApi.hpp>
#include <QVariantMap>

BackupHistoryModel::BackupHistoryModel(QObject* parent)
    : QObject(parent) {
}

QVariantList BackupHistoryModel::entries() const {
    return entries_;
}

bool BackupHistoryModel::apply(const QString& payload) {
    const auto history = btrfsbackup::kde::parse_history(payload);
    if (!history.has_value()) {
        return false;
    }

    QVariantList entries;
    for (const btrfsbackup::kde::HistoryEntry& item : *history) {
        QVariantMap entry;
        entry.insert(QStringLiteral("state"), item.state);
        entry.insert(QStringLiteral("errorCode"), item.error_code);
        entry.insert(QStringLiteral("sourceName"), item.source_name);
        entry.insert(QStringLiteral("targetName"), item.target_name);
        entry.insert(QStringLiteral("startedAt"), item.started_at);
        entry.insert(QStringLiteral("finishedAt"), item.finished_at);
        entry.insert(QStringLiteral("durationSeconds"), item.duration_seconds);
        entry.insert(QStringLiteral("sourceCount"), item.source_count);
        entry.insert(QStringLiteral("overallProgress"), item.overall_progress);
        entries.push_back(entry);
    }
    entries_ = entries;
    emit changed();
    return true;
}

void BackupHistoryModel::reset() {
    entries_.clear();
    emit changed();
}
