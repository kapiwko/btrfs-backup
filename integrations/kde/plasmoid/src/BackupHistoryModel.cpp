// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BackupHistoryModel.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

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
        QVariantMap entry;
        entry.insert(QStringLiteral("state"), item.value(QStringLiteral("state")).toString());
        entry.insert(QStringLiteral("errorCode"), item.value(QStringLiteral("errorCode")).toString());
        entry.insert(QStringLiteral("sourceName"), item.value(QStringLiteral("sourceName")).toString());
        entry.insert(QStringLiteral("targetName"), item.value(QStringLiteral("targetName")).toString());
        entry.insert(QStringLiteral("finishedAt"), item.value(QStringLiteral("finishedAt")).toString());
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
