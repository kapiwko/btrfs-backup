// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ManagerApi.hpp"

#include "Manager1Interface.h"

#include <QDBusMessage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace btrfsbackup::kde {

ManagerEventSubscriber::ManagerEventSubscriber(QDBusConnection bus, QObject* parent)
    : QObject(parent),
      manager_(new IoGithubBtrfsbackupManager1Interface(
          QLatin1String(manager_protocol::service_name),
          QLatin1String(manager_protocol::object_path),
          bus,
          this
      )) {
    connect(
        manager_,
        &IoGithubBtrfsbackupManager1Interface::ProfilesChanged,
        this,
        &ManagerEventSubscriber::profilesChanged
    );
    connect(
        manager_,
        &IoGithubBtrfsbackupManager1Interface::StatusChanged,
        this,
        &ManagerEventSubscriber::statusChanged
    );
    connect(
        manager_,
        &IoGithubBtrfsbackupManager1Interface::HistoryChanged,
        this,
        &ManagerEventSubscriber::historyChanged
    );
    connect(
        manager_,
        &IoGithubBtrfsbackupManager1Interface::DeviceStateChanged,
        this,
        &ManagerEventSubscriber::deviceStateChanged
    );
}

QDBusPendingCall manager_call(
    const QDBusConnection& bus,
    const QString& method,
    const QVariantList& arguments
) {
    QDBusMessage message = QDBusMessage::createMethodCall(
        QLatin1String(manager_protocol::service_name),
        QLatin1String(manager_protocol::object_path),
        QLatin1String(manager_protocol::interface_name),
        method
    );
    message.setArguments(arguments);
    return bus.asyncCall(message);
}

std::optional<ManagerCapabilities> parse_capabilities(const QString& payload) {
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8());
    if (!document.isObject()) {
        return std::nullopt;
    }

    const QJsonObject object = document.object();
    ManagerCapabilities result{
        .api_major = object.value(QStringLiteral("apiMajor")).toInt(-1),
        .public_status_schema_version = object.value(
                                                  QStringLiteral("publicStatusSchemaVersion")
        )
                                            .toInt(-1),
        .features = {},
    };
    const QJsonValue features = object.value(QStringLiteral("features"));
    if (!features.isArray()) {
        return std::nullopt;
    }
    for (const QJsonValue& feature : features.toArray()) {
        if (!feature.isString()) {
            return std::nullopt;
        }
        result.features.insert(feature.toString());
    }
    return result;
}

std::optional<QList<ProfileSummary>> parse_profiles(const QString& payload) {
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8());
    if (!document.isArray()) {
        return std::nullopt;
    }

    QList<ProfileSummary> result;
    for (const QJsonValue& value : document.array()) {
        if (!value.isObject()) {
            return std::nullopt;
        }
        const QJsonObject object = value.toObject();
        ProfileSummary profile{
            .id = object.value(QStringLiteral("profileId")).toString(),
            .name = object.value(QStringLiteral("name")).toString(),
            .target_name = object.value(QStringLiteral("targetName")).toString(),
        };
        if (profile.id.isEmpty()) {
            return std::nullopt;
        }
        result.push_back(std::move(profile));
    }
    return result;
}

std::optional<RunStatus> parse_status(const QString& payload) {
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8());
    if (!document.isObject()) {
        return std::nullopt;
    }
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("schemaVersion")).toInt(-1) != manager_protocol::public_status_schema_version) {
        return std::nullopt;
    }
    for (const auto* field : {
             "state",
             "errorCode",
             "sourceName",
             "targetName",
             "speedBps",
             "etaSeconds",
             "sourceProgress",
             "overallProgress",
             "progressAccuracy",
         }) {
        if (!object.contains(QLatin1String(field))) {
            return std::nullopt;
        }
    }

    return RunStatus{
        .run_id = object.value(QStringLiteral("runId")).toString(),
        .state = object.value(QStringLiteral("state")).toString(),
        .phase = object.value(QStringLiteral("phase")).toString(QStringLiteral("idle")),
        .activity = object.value(QStringLiteral("activity")).toString(QStringLiteral("idle")),
        .error_code = object.value(QStringLiteral("errorCode")).toString(),
        .source_name = object.value(QStringLiteral("sourceName")).toString(),
        .target_name = object.value(QStringLiteral("targetName")).toString(),
        .progress_accuracy = object.value(QStringLiteral("progressAccuracy")).toString(QStringLiteral("indeterminate")),
        .can_cancel = object.value(QStringLiteral("canCancel")).toBool(false),
        .speed_bps = static_cast<qint64>(object.value(QStringLiteral("speedBps")).toDouble()),
        .eta_seconds = static_cast<qint64>(object.value(QStringLiteral("etaSeconds")).toDouble(-1)),
        .source_progress = object.value(QStringLiteral("sourceProgress")).toInt(-1),
        .overall_progress = object.value(QStringLiteral("overallProgress")).toInt(-1),
    };
}

bool active_run_state(const QString& state) {
    return state == QStringLiteral("starting") || state == QStringLiteral("running") ||
        state == QStringLiteral("validating");
}

} // namespace btrfsbackup::kde
