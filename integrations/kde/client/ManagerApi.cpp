// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ManagerApi.hpp"

#include "Manager1Interface.h"

#include <QDBusMessage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <limits>

#include <state/document/RunStatusDocumentCodec.hpp>

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
    QJsonObject object = document.object();
    if (object.value(QStringLiteral("schemaVersion")).toInt(-1) != manager_protocol::public_status_schema_version) {
        return std::nullopt;
    }
    for (const QString& field : {
             QStringLiteral("lastSuccessAt"),
             QStringLiteral("lastAttemptAt"),
             QStringLiteral("lastAttemptState"),
         }) {
        if (!object.value(field).isString()) {
            return std::nullopt;
        }
    }
    const QString last_success_at = object.take(QStringLiteral("lastSuccessAt")).toString();
    const QString last_attempt_at = object.take(QStringLiteral("lastAttemptAt")).toString();
    const QString last_attempt_state = object.take(QStringLiteral("lastAttemptState")).toString();
    object[QStringLiteral("schemaVersion")] = 3;
    const auto decoded = btrfsbackup::state::document::RunStatusDocumentCodec{}.try_parse_public(
        QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString()
    );
    if (!decoded.has_value()) {
        return std::nullopt;
    }
    const auto& status = *decoded;
    constexpr auto max_qint64 = static_cast<std::uint64_t>(std::numeric_limits<qint64>::max());
    if (status.progress.speed_bps > max_qint64 ||
        (status.progress.eta_seconds.has_value() && *status.progress.eta_seconds > max_qint64)) {
        return std::nullopt;
    }
    return RunStatus{
        .run_id = status.run_id.has_value()
            ? QString::fromStdString(std::string(status.run_id->value()))
            : QString{},
        .state = QString::fromStdString(btrfsbackup::state::document::public_run_state_name(status)),
        .phase = QString::fromStdString(status.phase.value),
        .activity = QString::fromStdString(btrfsbackup::state::document::public_activity_name(status)),
        .error_code = QString::fromStdString(btrfsbackup::state::document::public_error_code_name(status.error_code)),
        .source_name = QString::fromStdString(status.source_name),
        .target_name = QString::fromStdString(status.target_name),
        .progress_accuracy = QString::fromStdString(btrfsbackup::state::progress_accuracy_name(status.progress.accuracy)),
        .last_success_at = last_success_at,
        .last_attempt_at = last_attempt_at,
        .last_attempt_state = last_attempt_state,
        .can_cancel = status.can_cancel,
        .speed_bps = static_cast<qint64>(status.progress.speed_bps),
        .eta_seconds = status.progress.eta_seconds.has_value() ? static_cast<qint64>(*status.progress.eta_seconds) : -1,
        .source_progress = status.progress.source_percent.value_or(-1),
        .overall_progress = status.progress.overall_percent.value_or(-1),
    };
}

std::optional<OperationResult> parse_operation_result(const QString& payload) {
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8());
    if (!document.isObject()) {
        return std::nullopt;
    }

    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("schemaVersion")).toInt(-1) !=
        manager_protocol::operation_result_schema_version) {
        return std::nullopt;
    }
    for (const QString& field : {
             QStringLiteral("operation"),
             QStringLiteral("operationId"),
             QStringLiteral("profileId"),
         }) {
        if (!object.value(field).isString() || object.value(field).toString().isEmpty()) {
            return std::nullopt;
        }
    }
    if (!object.value(QStringLiteral("accepted")).isBool()) {
        return std::nullopt;
    }
    const QJsonValue run_id = object.value(QStringLiteral("runId"));
    if (!run_id.isUndefined() && !run_id.isString()) {
        return std::nullopt;
    }
    return OperationResult{
        .operation = object.value(QStringLiteral("operation")).toString(),
        .operation_id = object.value(QStringLiteral("operationId")).toString(),
        .profile_id = object.value(QStringLiteral("profileId")).toString(),
        .run_id = run_id.toString(),
        .accepted = object.value(QStringLiteral("accepted")).toBool(),
    };
}

bool active_run_state(const QString& state) {
    return state == QStringLiteral("starting") || state == QStringLiteral("running") ||
        state == QStringLiteral("validating");
}

} // namespace btrfsbackup::kde
