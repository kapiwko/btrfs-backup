// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ManagerApi.hpp"

#include "Manager1Interface.h"

#include <QDBusMessage>
#include <QDBusError>
#include <QDBusUnixFileDescriptor>
#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include <algorithm>
#include <limits>
#include <utility>

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

ManagerClient::ManagerClient(QDBusConnection bus) : bus_(std::move(bus)) {
}

QDBusPendingCall ManagerClient::call(const QString& method, const QVariantList& arguments) const {
    return manager_call(bus_, method, arguments);
}

QDBusPendingCall ManagerClient::capabilities() const {
    return call(QLatin1String(manager_protocol::method::get_capabilities));
}

QDBusPendingCall ManagerClient::profiles() const {
    return call(QLatin1String(manager_protocol::method::list_profiles));
}

QDBusPendingCall ManagerClient::status(const QString& profile_id) const {
    return call(QLatin1String(manager_protocol::method::get_status), {profile_id});
}

QDBusPendingCall ManagerClient::deviceState(const QString& profile_id) const {
    return call(QLatin1String(manager_protocol::method::get_device_state), {profile_id});
}

QDBusPendingCall ManagerClient::history(const QString& profile_id, uint offset, uint limit) const {
    return call(QLatin1String(manager_protocol::method::get_history_sanitized), {profile_id, offset, limit});
}

QDBusPendingCall ManagerClient::startBackup(const QString& profile_id) const {
    return call(QLatin1String(manager_protocol::method::start_backup), {profile_id});
}

QDBusPendingCall ManagerClient::cancelBackup(const QString& profile_id, const QString& run_id) const {
    return call(QLatin1String(manager_protocol::method::cancel_backup), {profile_id, run_id});
}

QDBusPendingCall ManagerClient::ejectTarget(const QString& profile_id) const {
    return call(QLatin1String(manager_protocol::method::eject_target), {profile_id});
}

QDBusPendingCall ManagerClient::resolveBackupCoverage(const QString& local_path) const {
    const QByteArray encoded_path = QFile::encodeName(local_path);
    const int descriptor = ::open(encoded_path.constData(), O_PATH | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) {
        return QDBusPendingCall::fromError(QDBusError(
            QDBusError::Failed,
            QStringLiteral("Cannot open the selected local path: %1").arg(QString::fromLocal8Bit(std::strerror(errno)))
        ));
    }
    const QDBusUnixFileDescriptor entry(descriptor);
    ::close(descriptor);
    return call(
        QLatin1String(manager_protocol::method::resolve_backup_coverage_by_fd),
        {QVariant::fromValue(entry)}
    );
}

QDBusPendingCall ManagerClient::openBrowseSession(const QString& profile_id) const {
    return call(QLatin1String(manager_protocol::method::open_browse_session), {profile_id});
}

QDBusPendingCall ManagerClient::renewBrowseSession(const QString& session_id) const {
    return call(QLatin1String(manager_protocol::method::renew_browse_session), {session_id});
}

QDBusPendingCall ManagerClient::beginBrowseOperation(const QString& session_id) const {
    return call(QLatin1String(manager_protocol::method::begin_browse_operation), {session_id});
}

QDBusPendingCall ManagerClient::endBrowseOperation(const QString& session_id, const QString& lease_id) const {
    return call(QLatin1String(manager_protocol::method::end_browse_operation), {session_id, lease_id});
}

QDBusPendingCall ManagerClient::closeBrowseSession(const QString& session_id) const {
    return call(QLatin1String(manager_protocol::method::close_browse_session), {session_id});
}

QDBusPendingCall ManagerClient::listBrowseDirectory(const QString& session_id, const QString& path) const {
    return call(QLatin1String(manager_protocol::method::list_browse_directory), {session_id, path});
}

QDBusPendingCall ManagerClient::listBrowseDirectoryPage(
    const QString& session_id,
    const QString& path,
    const QString& continuation_token,
    uint limit
) const {
    return call(
        QLatin1String(manager_protocol::method::list_browse_directory_page),
        {session_id, path, continuation_token, limit}
    );
}

QDBusPendingCall ManagerClient::listPreviousVersions(
    const QString& session_id,
    const QString& profile_id,
    const QString& source_id,
    const QString& relative_path,
    const QString& continuation_token,
    uint limit
) const {
    return call(
        QLatin1String(manager_protocol::method::list_previous_versions),
        {session_id, profile_id, source_id, relative_path, continuation_token, limit}
    );
}

QDBusPendingCall ManagerClient::inspectBrowseEntry(const QString& session_id, const QString& path) const {
    return call(QLatin1String(manager_protocol::method::inspect_browse_entry), {session_id, path});
}

QDBusPendingCall ManagerClient::inspectBrowseRepository(const QString& session_id) const {
    return call(QLatin1String(manager_protocol::method::inspect_browse_repository), {session_id});
}

QDBusPendingCall ManagerClient::openBrowseFile(const QString& session_id, const QString& path) const {
    return call(QLatin1String(manager_protocol::method::open_browse_file), {session_id, path});
}

QDBusPendingCall ManagerClient::openBrowseEntry(const QString& session_id, const QString& path) const {
    return call(QLatin1String(manager_protocol::method::open_browse_entry), {session_id, path});
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
        .history_schema_version = object.value(QStringLiteral("historySchemaVersion")).toInt(-1),
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
            .enabled = object.value(QStringLiteral("enabled")).toBool(true),
            .target_name = object.value(QStringLiteral("targetName")).toString(),
            .sources = {},
            .configuration_valid = object.value(QStringLiteral("configurationValid")).toBool(true),
            .configuration_error_code = object.value(QStringLiteral("configurationErrorCode")).toString(),
        };
        const QJsonValue sources = object.value(QStringLiteral("sources"));
        if (profile.id.isEmpty() || !sources.isArray()) {
            return std::nullopt;
        }
        for (const QJsonValue& source_value : sources.toArray()) {
            if (!source_value.isObject()) {
                return std::nullopt;
            }
            const QJsonObject source = source_value.toObject();
            const ProfileSourceSummary summary{
                .id = source.value(QStringLiteral("id")).toString(),
                .name = source.value(QStringLiteral("name")).toString(),
            };
            if (summary.id.isEmpty()) {
                return std::nullopt;
            }
            profile.sources.push_back(summary);
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
    for (const QString& field : {QStringLiteral("startedAt"), QStringLiteral("updatedAt")}) {
        if (!object.value(field).isString()) {
            return std::nullopt;
        }
    }
    for (const QString& field : {QStringLiteral("sourceIndex"), QStringLiteral("sourceCount")}) {
        const QJsonValue value = object.value(field);
        if (!value.isDouble() || value.toDouble() != value.toInt(-1)) {
            return std::nullopt;
        }
    }
    const QString started_at = object.take(QStringLiteral("startedAt")).toString();
    const QString updated_at = object.take(QStringLiteral("updatedAt")).toString();
    const int source_index = object.take(QStringLiteral("sourceIndex")).toInt(-1);
    const int source_count = object.take(QStringLiteral("sourceCount")).toInt(-1);
    if (source_index < 0 || source_count < 0 || source_index > source_count) {
        return std::nullopt;
    }
    for (const QString& timestamp : {started_at, updated_at}) {
        if (!timestamp.isEmpty() && !QDateTime::fromString(timestamp, Qt::ISODate).isValid()) {
            return std::nullopt;
        }
    }
    object[QStringLiteral("schemaVersion")] = 1;
    const auto decoded = btrfsbackup::state::document::RunStatusDocumentCodec{}.try_parse_public(
        QJsonDocument(object).toJson(QJsonDocument::Compact).toStdString()
    );
    if (!decoded.has_value()) {
        return std::nullopt;
    }
    const auto& status = *decoded;
    constexpr auto max_qint64 = static_cast<std::uint64_t>(std::numeric_limits<qint64>::max());
    if (status.progress.bytes_processed > max_qint64 ||
        (status.progress.bytes_total_estimated.has_value() && *status.progress.bytes_total_estimated > max_qint64) ||
        status.progress.speed_bps > max_qint64 ||
        (status.progress.eta_seconds.has_value() && *status.progress.eta_seconds > max_qint64)) {
        return std::nullopt;
    }
    return RunStatus{
        .run_id = status.run_id.has_value()
            ? QString::fromStdString(std::string(status.run_id->value()))
            : QString{},
        .operation_kind = QString::fromStdString(
            btrfsbackup::state::document::public_operation_kind_name(status)
        ),
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
        .started_at = started_at,
        .updated_at = updated_at,
        .can_cancel = status.can_cancel,
        .bytes_processed = static_cast<qint64>(status.progress.bytes_processed),
        .bytes_total_estimated = status.progress.bytes_total_estimated.has_value()
            ? static_cast<qint64>(*status.progress.bytes_total_estimated)
            : 0,
        .speed_bps = static_cast<qint64>(status.progress.speed_bps),
        .eta_seconds = status.progress.eta_seconds.has_value() ? static_cast<qint64>(*status.progress.eta_seconds) : -1,
        .source_progress = status.progress.source_percent.value_or(-1),
        .overall_progress = status.progress.overall_percent.value_or(-1),
        .source_index = source_index,
        .source_count = source_count,
    };
}

std::optional<QList<HistoryEntry>> parse_history(const QString& payload) {
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8());
    if (!document.isArray())
        return std::nullopt;

    QList<HistoryEntry> result;
    for (const QJsonValue& value : document.array()) {
        if (!value.isObject())
            return std::nullopt;
        const QJsonObject object = value.toObject();
        if (object.value(QStringLiteral("schemaVersion")).toInt(-1) != manager_protocol::history_schema_version)
            return std::nullopt;
        const QString started_at = object.value(QStringLiteral("startedAt")).toString();
        const QString finished_at = object.value(QStringLiteral("finishedAt")).toString();
        const QDateTime started = QDateTime::fromString(started_at, Qt::ISODate);
        const QDateTime finished = QDateTime::fromString(finished_at, Qt::ISODate);
        if ((!started_at.isEmpty() && !started.isValid()) || (!finished_at.isEmpty() && !finished.isValid()))
            return std::nullopt;
        const auto optional_integer = [&](const char* name, int fallback) -> std::optional<int> {
            const QJsonValue number = object.value(QLatin1String(name));
            if (number.isUndefined())
                return fallback;
            if (!number.isDouble() || number.toDouble() != number.toInt())
                return std::nullopt;
            return number.toInt();
        };
        const auto source_count = optional_integer("sourceCount", 0);
        const auto overall_progress = optional_integer("overallProgress", -1);
        const QJsonValue transferred_value = object.value(QStringLiteral("bytesTransferred"));
        const double bytes = transferred_value.isUndefined() ? 0 : transferred_value.toDouble(-1);
        if (!source_count.has_value() || !overall_progress.has_value() || bytes < 0 ||
            bytes > static_cast<double>(std::numeric_limits<qint64>::max()))
            return std::nullopt;
        result.push_back({
            .state = object.value(QStringLiteral("state")).toString(),
            .error_code = object.value(QStringLiteral("errorCode")).toString(),
            .source_name = object.value(QStringLiteral("sourceName")).toString(),
            .target_name = object.value(QStringLiteral("targetName")).toString(),
            .started_at = started_at,
            .finished_at = finished_at,
            .duration_seconds = started.isValid() && finished.isValid()
                ? std::max<qint64>(0, started.secsTo(finished))
                : -1,
            .bytes_transferred = static_cast<qint64>(bytes),
            .source_count = *source_count,
            .overall_progress = *overall_progress,
        });
    }
    return result;
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

std::optional<BrowseSessionInfo> parse_browse_session(const QString& payload) {
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8());
    if (!document.isObject())
        return std::nullopt;
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("schemaVersion")).toInt(-1) != manager_protocol::browse_session_schema_version ||
        !object.value(QStringLiteral("readOnly")).toBool(false))
        return std::nullopt;
    BrowseSessionInfo result{
        .session_id = object.value(QStringLiteral("sessionId")).toString(),
        .profile_id = object.value(QStringLiteral("profileId")).toString(),
        .expires_at = QDateTime::fromString(object.value(QStringLiteral("expiresAt")).toString(), Qt::ISODate),
        .read_only = true,
    };
    if (result.session_id.isEmpty() || result.profile_id.isEmpty() || !result.expires_at.isValid())
        return std::nullopt;
    return result;
}

std::optional<QList<BackupCoverage>> parse_backup_coverage(const QString& payload) {
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8());
    if (!document.isArray())
        return std::nullopt;
    QList<BackupCoverage> result;
    for (const QJsonValue& value : document.array()) {
        if (!value.isObject())
            return std::nullopt;
        const QJsonObject object = value.toObject();
        BackupCoverage item{
            object.value(QStringLiteral("profileId")).toString(),
            object.value(QStringLiteral("sourceId")).toString(),
            object.value(QStringLiteral("relativePath")).toString(),
        };
        if (item.profile_id.isEmpty() || item.source_id.isEmpty() || item.relative_path.isEmpty() ||
            item.relative_path.startsWith(u'/') || item.relative_path.split(u'/').contains(QStringLiteral("..")))
            return std::nullopt;
        result.push_back(std::move(item));
    }
    return result;
}

bool active_run_state(const QString& state) {
    return state == QStringLiteral("starting") || state == QStringLiteral("running") ||
        state == QStringLiteral("validating");
}

} // namespace btrfsbackup::kde
