// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "NotificationDeduplicator.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <utility>

namespace btrfsbackup::kde::monitor {

namespace {

constexpr int state_schema_version = 1;
constexpr int retention_days = 90;
constexpr std::size_t maximum_entries = 256;

QString default_state_path() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) +
        QStringLiteral("/btrfs-backup-kde/notifications.json");
}

} // namespace

NotificationDeduplicator::NotificationDeduplicator(QString state_path)
    : state_path_(state_path.isEmpty() ? default_state_path() : std::move(state_path)) {
    load();
}

bool NotificationDeduplicator::claim(
    const QString& profile_id,
    const QString& run_id,
    const QString& event_kind,
    const QDateTime& now
) {
    prune(now);
    const auto duplicate = std::ranges::find_if(entries_, [&](const Entry& entry) {
        return entry.profile_id == profile_id && entry.run_id == run_id && entry.event_kind == event_kind;
    });
    if (duplicate != entries_.end()) {
        return false;
    }

    entries_.push_back({profile_id, run_id, event_kind, now.toUTC()});
    prune(now);
    save();
    return true;
}

void NotificationDeduplicator::load() {
    QFile file(state_path_);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject() ||
        document.object().value(QStringLiteral("schemaVersion")).toInt(-1) != state_schema_version) {
        return;
    }
    const QJsonValue entries = document.object().value(QStringLiteral("entries"));
    if (!entries.isArray()) {
        return;
    }
    for (const QJsonValue& value : entries.toArray()) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        const QDateTime handled_at = QDateTime::fromString(
            object.value(QStringLiteral("handledAt")).toString(),
            Qt::ISODate
        );
        const Entry entry{
            .profile_id = object.value(QStringLiteral("profileId")).toString(),
            .run_id = object.value(QStringLiteral("runId")).toString(),
            .event_kind = object.value(QStringLiteral("eventKind")).toString(),
            .handled_at = handled_at,
        };
        if (!entry.profile_id.isEmpty() && !entry.run_id.isEmpty() &&
            !entry.event_kind.isEmpty() && entry.handled_at.isValid()) {
            entries_.push_back(entry);
        }
    }
    prune(QDateTime::currentDateTimeUtc());
}

void NotificationDeduplicator::prune(const QDateTime& now) {
    const QDateTime oldest = now.toUTC().addDays(-retention_days);
    std::erase_if(entries_, [&](const Entry& entry) {
        return entry.handled_at < oldest;
    });
    if (entries_.size() > maximum_entries) {
        entries_.erase(entries_.begin(), entries_.end() - static_cast<std::ptrdiff_t>(maximum_entries));
    }
}

void NotificationDeduplicator::save() const {
    QJsonArray entries;
    for (const Entry& entry : entries_) {
        entries.push_back(QJsonObject{
            {QStringLiteral("profileId"), entry.profile_id},
            {QStringLiteral("runId"), entry.run_id},
            {QStringLiteral("eventKind"), entry.event_kind},
            {QStringLiteral("handledAt"), entry.handled_at.toUTC().toString(Qt::ISODate)},
        });
    }
    const QFileInfo state_file(state_path_);
    QDir{}.mkpath(state_file.absolutePath());
    QSaveFile file(state_path_);
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }
    file.write(QJsonDocument(QJsonObject{
                                 {QStringLiteral("schemaVersion"), state_schema_version},
                                 {QStringLiteral("entries"), entries},
                             })
                   .toJson(QJsonDocument::Compact));
    file.commit();
}

} // namespace btrfsbackup::kde::monitor
