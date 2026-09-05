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

std::optional<QUrl> version_target_url(
    const QString& profile_id,
    const QString& snapshot_id,
    const QString& requested_path
) {
    if (profile_id.isEmpty() || snapshot_id.isEmpty() || requested_path.isEmpty() ||
        requested_path.startsWith(u'/') || requested_path.split(u'/').contains(u".."_s))
        return std::nullopt;
    QUrl target;
    target.setScheme(u"btrfsbackup"_s);
    QString target_path = u"/"_s + profile_id + u"/"_s + snapshot_id;
    if (requested_path != u"."_s)
        target_path += u"/"_s + requested_path;
    target.setPath(target_path);
    return target;
}

std::optional<PreviousVersionsPage> parse_previous_versions_page(const QString& payload) {
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8());
    if (!document.isObject())
        return std::nullopt;
    const QJsonObject root = document.object();
    if (root.value(u"schemaVersion"_s).toInt() != 1 ||
        !root.value(u"entries"_s).isArray() || !root.value(u"continuationToken"_s).isString())
        return std::nullopt;
    PreviousVersionsPage result;
    for (const QJsonValue& value : root.value(u"entries"_s).toArray()) {
        if (!value.isObject())
            return std::nullopt;
        const QJsonObject object = value.toObject();
        const QString kind = object.value(u"kind"_s).toString();
        PreviousVersion version{
            object.value(u"snapshotId"_s).toString(),
            QDateTime::fromString(object.value(u"createdAt"_s).toString(), Qt::ISODate),
            kind == u"directory"_s,
            static_cast<std::uint64_t>(object.value(u"size"_s).toDouble(-1)),
            static_cast<std::uint32_t>(object.value(u"mode"_s).toDouble(-1)),
            static_cast<std::int64_t>(object.value(u"modifiedAt"_s).toDouble()),
        };
        if (version.snapshot_id.isEmpty() || !version.created_at.isValid() ||
            (kind != u"directory"_s && kind != u"file"_s) || !object.value(u"size"_s).isDouble() ||
            !object.value(u"mode"_s).isDouble() || !object.value(u"modifiedAt"_s).isDouble())
            return std::nullopt;
        result.entries.push_back(std::move(version));
    }
    result.continuation_token = root.value(u"continuationToken"_s).toString();
    return result;
}

bool previous_versions_method_unavailable(const QString& dbus_error_name) {
    return dbus_error_name == u"org.freedesktop.DBus.Error.UnknownMethod"_s;
}

} // namespace btrfsbackup::kde::kio
