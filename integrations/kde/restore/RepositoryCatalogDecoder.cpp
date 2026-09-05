// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "RepositoryCatalogDecoder.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <core/RuntimeTime.hpp>
#include <restore/RestoreError.hpp>

#include <utility>

using Qt::StringLiterals::operator""_s;

namespace btrfsbackup::kde::restore {
namespace {

[[noreturn]] void invalid_catalog(const std::string& detail) {
    throw btrfsbackup::restore::RestoreError(
        btrfsbackup::restore::RestoreErrorCode::CatalogInvalid,
        "manager returned an invalid repository catalog: " + detail
    );
}

QString required_string(const QJsonObject& object, const QString& key) {
    const QJsonValue value = object.value(key);
    if (!value.isString() || value.toString().isEmpty())
        invalid_catalog("missing non-empty string '" + key.toStdString() + "'");
    return value.toString();
}

btrfsbackup::RuntimeTimePoint required_time(const QJsonObject& object, const QString& key) {
    const auto value = btrfsbackup::parse_utc_timestamp(required_string(object, key).toStdString());
    if (!value)
        invalid_catalog("invalid UTC timestamp '" + key.toStdString() + "'");
    return *value;
}

} // namespace

btrfsbackup::restore::RepositoryCatalog RepositoryCatalogDecoder::decode(
    const QString& payload,
    const std::filesystem::path& root_path
) const {
    QJsonParseError parse_error;
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        invalid_catalog("invalid JSON document");

    const QJsonObject root = document.object();
    if (root.value(u"schemaVersion"_s).toInt(-1) != 1)
        throw btrfsbackup::restore::RestoreError(
            btrfsbackup::restore::RestoreErrorCode::RepositoryFormatUnsupported,
            "manager returned an unsupported repository catalog schema"
        );
    if (!root.value(u"features"_s).isArray() || !root.value(u"snapshots"_s).isArray() ||
        !root.value(u"generation"_s).isDouble())
        invalid_catalog("missing features, snapshots, or generation");

    std::vector<std::string> features;
    for (const QJsonValue& value : root.value(u"features"_s).toArray()) {
        if (!value.isString())
            invalid_catalog("repository feature is not a string");
        features.push_back(value.toString().toStdString());
    }

    std::vector<btrfsbackup::restore::CatalogSnapshot> snapshots;
    for (const QJsonValue& value : root.value(u"snapshots"_s).toArray()) {
        if (!value.isObject())
            invalid_catalog("snapshot is not an object");
        const QJsonObject snapshot = value.toObject();
        snapshots.push_back({
            .snapshot_id = required_string(snapshot, u"snapshotId"_s).toStdString(),
            .host_id = required_string(snapshot, u"hostId"_s).toStdString(),
            .profile_id = required_string(snapshot, u"profileId"_s).toStdString(),
            .source_id = required_string(snapshot, u"sourceId"_s).toStdString(),
            .repository_path = btrfsbackup::restore::RelativeRestorePath{
                required_string(snapshot, u"relativePath"_s).toStdString()
            },
            .created_at = required_time(snapshot, u"createdAt"_s),
            .uuid = required_string(snapshot, u"uuid"_s).toStdString(),
            .received_uuid = snapshot.value(u"receivedUuid"_s).toString().toStdString(),
            .parent_uuid = snapshot.value(u"parentUuid"_s).toString().toStdString(),
            .verified = snapshot.value(u"verified"_s).toBool(false),
        });
    }

    return {
        root_path,
        {
            .repository_id = required_string(root, u"repositoryId"_s).toStdString(),
            .target_filesystem_uuid = required_string(root, u"targetFilesystemUuid"_s).toStdString(),
            .created_at = required_time(root, u"createdAt"_s),
            .features = std::move(features),
        },
        static_cast<std::uint64_t>(root.value(u"generation"_s).toDouble()),
        std::move(snapshots),
    };
}

} // namespace btrfsbackup::kde::restore
