// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BackupHistoryModel.hpp"

#include <core/ManagerProtocol.hpp>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QVariantMap>

#include <algorithm>

namespace {

int json_int(const QJsonObject& object, const char* key, int fallback) {
    const auto value = object.value(QLatin1String(key));
    return value.isDouble() ? value.toInt() : fallback;
}

} // namespace

BackupHistoryModel::BackupHistoryModel(QObject* parent)
    : QObject(parent) {
}

QVariantList BackupHistoryModel::entries() const {
    return entries_;
}

bool BackupHistoryModel::apply(const QString& payload) {
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8());
    if (!document.isArray()) {
        return false;
    }

    QVariantList entries;
    for (const QJsonValue& value : document.array()) {
        if (!value.isObject()) {
            return false;
        }
        const QJsonObject item = value.toObject();
        if (json_int(item, "schemaVersion", -1) != btrfsbackup::manager_protocol::history_schema_version) {
            return false;
        }
        const QString started_at = item.value(QStringLiteral("startedAt")).toString();
        const QString finished_at = item.value(QStringLiteral("finishedAt")).toString();
        const QDateTime started = QDateTime::fromString(started_at, Qt::ISODate);
        const QDateTime finished = QDateTime::fromString(finished_at, Qt::ISODate);
        QVariantMap entry;
        entry.insert(QStringLiteral("state"), item.value(QStringLiteral("state")).toString());
        entry.insert(QStringLiteral("errorCode"), item.value(QStringLiteral("errorCode")).toString());
        entry.insert(QStringLiteral("sourceName"), item.value(QStringLiteral("sourceName")).toString());
        entry.insert(QStringLiteral("targetName"), item.value(QStringLiteral("targetName")).toString());
        entry.insert(QStringLiteral("startedAt"), started_at);
        entry.insert(QStringLiteral("finishedAt"), finished_at);
        entry.insert(
            QStringLiteral("durationSeconds"),
            started.isValid() && finished.isValid()
                ? static_cast<int>(std::max<qint64>(0, started.secsTo(finished)))
                : -1
        );
        entry.insert(QStringLiteral("sourceCount"), json_int(item, "sourceCount", 0));
        entry.insert(QStringLiteral("overallProgress"), json_int(item, "overallProgress", -1));
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
