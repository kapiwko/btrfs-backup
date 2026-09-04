// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RepositorySnapshotModel.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

using Qt::StringLiterals::operator""_s;

namespace btrfsbackup::kde::kio {

std::optional<QHash<QString, RepositorySnapshot>> parse_repository_snapshots(const QString& payload) {
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8());
    if (!document.isObject())
        return std::nullopt;
    const QJsonObject root = document.object();
    if (root.value(u"schemaVersion"_s).toInt() != 1 || !root.value(u"snapshots"_s).isArray())
        return std::nullopt;
    QHash<QString, RepositorySnapshot> result;
    for (const QJsonValue& value : root.value(u"snapshots"_s).toArray()) {
        if (!value.isObject())
            return std::nullopt;
        const QJsonObject object = value.toObject();
        RepositorySnapshot snapshot{
            object.value(u"snapshotId"_s).toString(),
            object.value(u"profileId"_s).toString(),
            object.value(u"sourceId"_s).toString(),
            object.value(u"relativePath"_s).toString(),
            QDateTime::fromString(object.value(u"createdAt"_s).toString(), Qt::ISODate),
            object.value(u"verified"_s).toBool(false),
        };
        if (snapshot.id.isEmpty() || snapshot.profile_id.isEmpty() || snapshot.source_id.isEmpty() || snapshot.repository_path.isEmpty() ||
            !snapshot.created_at.isValid() || result.contains(snapshot.id))
            return std::nullopt;
        result.insert(snapshot.id, std::move(snapshot));
    }
    return result;
}

std::vector<RepositorySnapshot> matching_versions(
    const QHash<QString, RepositorySnapshot>& snapshots,
    const QString& profile_id,
    const QString& source_id
) {
    std::vector<RepositorySnapshot> result;
    result.reserve(snapshots.size());
    for (const RepositorySnapshot& snapshot : snapshots) {
        if (snapshot.verified && snapshot.profile_id == profile_id && snapshot.source_id == source_id)
            result.push_back(snapshot);
    }
    std::ranges::sort(result, std::greater{}, &RepositorySnapshot::created_at);
    return result;
}

} // namespace btrfsbackup::kde::kio
