// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BackupStatusModel.hpp"

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMap>
#include <QVariantMap>

namespace {

constexpr auto manager_service = "io.github.btrfsbackup.Manager1";
constexpr auto manager_path = "/io/github/btrfsbackup/Manager1";
constexpr auto manager_interface = "io.github.btrfsbackup.Manager1";
constexpr int poll_interval_ms = 1000;

qint64 json_int64(const QJsonObject& object, const char* key, qint64 fallback = 0) {
    const auto value = object.value(QLatin1String(key));
    return value.isDouble() ? static_cast<qint64>(value.toDouble()) : fallback;
}

int json_int(const QJsonObject& object, const char* key, int fallback = 0) {
    const auto value = object.value(QLatin1String(key));
    return value.isDouble() ? value.toInt() : fallback;
}

QString parseError(const QJsonParseError& error) {
    if (error.error == QJsonParseError::NoError) {
        return BackupStatusModel::tr("Invalid manager response.");
    }
    return BackupStatusModel::tr("Invalid manager response: %1").arg(error.errorString());
}

QDBusPendingCall managerCall(const QDBusConnection& bus, const QString& method, const QVariantList& arguments = {}) {
    QDBusMessage message = QDBusMessage::createMethodCall(
        QLatin1String(manager_service),
        QLatin1String(manager_path),
        QLatin1String(manager_interface),
        method
    );
    message.setArguments(arguments);
    return bus.asyncCall(message);
}

} // namespace

BackupStatusModel::BackupStatusModel(QObject* parent)
    : QObject(parent), bus_(QDBusConnection::systemBus()), service_watcher_(QLatin1String(manager_service), bus_, QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this) {
    poll_timer_.setInterval(poll_interval_ms);
    connect(&poll_timer_, &QTimer::timeout, this, &BackupStatusModel::refresh);
    connect(&service_watcher_, &QDBusServiceWatcher::serviceRegistered, this, [this]() {
        if (active_) {
            connectToManager();
        }
    });
    connect(&service_watcher_, &QDBusServiceWatcher::serviceUnregistered, this, [this]() {
        if (active_) {
            managerUnavailable();
        }
    });
}

QString BackupStatusModel::profile() const {
    return profile_;
}

void BackupStatusModel::setProfile(const QString& profile) {
    if (profile_ == profile) {
        return;
    }
    profile_ = profile;
    profile_name_.clear();
    run_id_.clear();
    state_ = QStringLiteral("unknown");
    phase_ = QStringLiteral("idle");
    activity_ = QStringLiteral("idle");
    can_cancel_ = false;
    target_state_ = QStringLiteral("unknown");
    target_connected_ = false;
    target_unlocked_ = false;
    target_mounted_ = false;
    safe_to_remove_ = false;
    history_.clear();
    last_operation_.clear();
    ++generation_;
    status_request_pending_ = false;
    device_request_pending_ = false;
    history_request_pending_ = false;
    operation_pending_ = false;
    emit profileChanged();
    emit statusChanged();
    emit targetChanged();
    emit historyChanged();
    emit operationChanged();
    if (active_ && capabilities_verified_) {
        requestProfiles();
        requestStatus();
        requestDeviceState();
        requestHistory();
    } else if (active_) {
        connectToManager();
    }
}

bool BackupStatusModel::managerConnected() const {
    return manager_connected_;
}

QVariantList BackupStatusModel::profiles() const {
    return profiles_;
}

QString BackupStatusModel::profileName() const {
    return profile_name_;
}

QString BackupStatusModel::state() const {
    return state_;
}

QString BackupStatusModel::runId() const {
    return run_id_;
}

QString BackupStatusModel::phase() const {
    return phase_;
}

QString BackupStatusModel::activity() const {
    return activity_;
}

bool BackupStatusModel::canCancel() const {
    return can_cancel_ && !run_id_.isEmpty() && supports(QStringLiteral("cancel-backup"));
}

QString BackupStatusModel::currentSourceName() const {
    return current_source_name_;
}

QString BackupStatusModel::targetName() const {
    return target_name_;
}

qint64 BackupStatusModel::speedBps() const {
    return speed_bps_;
}

int BackupStatusModel::etaSeconds() const {
    return eta_seconds_;
}

int BackupStatusModel::sourceProgress() const {
    return source_progress_;
}

int BackupStatusModel::overallProgress() const {
    return overall_progress_;
}

QString BackupStatusModel::progressAccuracy() const {
    return progress_accuracy_;
}

QString BackupStatusModel::errorCode() const {
    return error_code_;
}

QString BackupStatusModel::targetState() const {
    return target_state_;
}

bool BackupStatusModel::targetConnected() const {
    return target_connected_;
}

bool BackupStatusModel::targetUnlocked() const {
    return target_unlocked_;
}

bool BackupStatusModel::targetMounted() const {
    return target_mounted_;
}

bool BackupStatusModel::safeToRemove() const {
    return safe_to_remove_;
}

QVariantList BackupStatusModel::history() const {
    return history_;
}

bool BackupStatusModel::operationPending() const {
    return operation_pending_;
}

QString BackupStatusModel::lastOperation() const {
    return last_operation_;
}

QString BackupStatusModel::lastError() const {
    return last_error_;
}

void BackupStatusModel::start() {
    active_ = true;
    setLastError(QString());
    connectToManager();
}

void BackupStatusModel::stop() {
    active_ = false;
    poll_timer_.stop();
    ++generation_;
    status_request_pending_ = false;
    device_request_pending_ = false;
    history_request_pending_ = false;
    capabilities_verified_ = false;
    operation_pending_ = false;
    emit operationChanged();
    setManagerConnected(false);
}

void BackupStatusModel::refreshNow() {
    if (!active_) {
        start();
        return;
    }
    if (!capabilities_verified_) {
        connectToManager();
        return;
    }
    requestProfiles();
    requestStatus();
    requestDeviceState();
    requestHistory();
}

void BackupStatusModel::startBackup() {
    requestOperation(QStringLiteral("StartBackup"), {profile_});
}

void BackupStatusModel::cancelBackup() {
    if (run_id_.isEmpty()) {
        setLastError(tr("No active backup run can be cancelled."));
        return;
    }
    requestOperation(QStringLiteral("CancelBackup"), {profile_, run_id_});
}

void BackupStatusModel::validateTarget() {
    requestOperation(QStringLiteral("ValidateTarget"), {profile_});
}

void BackupStatusModel::ejectTarget() {
    requestOperation(QStringLiteral("EjectTarget"), {profile_});
}

void BackupStatusModel::connectToManager() {
    poll_timer_.stop();
    capabilities_verified_ = false;
    status_request_pending_ = false;
    device_request_pending_ = false;
    history_request_pending_ = false;
    const quint64 request_generation = ++generation_;
    auto* watcher = new QDBusPendingCallWatcher(managerCall(bus_, QStringLiteral("GetCapabilities")), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, request_generation](QDBusPendingCallWatcher*) {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        if (!active_ || request_generation != generation_) {
            return;
        }
        if (reply.isError()) {
            managerUnavailable();
            return;
        }

        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(reply.value().toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            setLastError(parseError(error));
            managerUnavailable();
            return;
        }
        const QJsonObject capabilities = document.object();
        if (json_int(capabilities, "apiMajor", -1) != 1 || json_int(capabilities, "publicStatusSchemaVersion", -1) != 3) {
            setLastError(tr("The backup manager API is not compatible with this widget."));
            managerUnavailable();
            return;
        }

        features_.clear();
        for (const QJsonValue& feature : capabilities.value(QStringLiteral("features")).toArray()) {
            if (feature.isString()) {
                features_.insert(feature.toString());
            }
        }

        capabilities_verified_ = true;
        setManagerConnected(true);
        setLastError(QString());
        requestProfiles();
        requestStatus();
        requestDeviceState();
        requestHistory();
        poll_timer_.start();
    });
}

void BackupStatusModel::refresh() {
    if (active_ && capabilities_verified_) {
        requestStatus();
        requestDeviceState();
    }
}

void BackupStatusModel::requestDeviceState() {
    if (!supports(QStringLiteral("device-state")) || device_request_pending_) {
        return;
    }
    device_request_pending_ = true;
    const quint64 request_generation = generation_;
    const QString requested_profile = profile_;
    auto* watcher = new QDBusPendingCallWatcher(managerCall(bus_, QStringLiteral("GetDeviceState"), {requested_profile}), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, request_generation, requested_profile](QDBusPendingCallWatcher*) {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        if (request_generation == generation_) {
            device_request_pending_ = false;
        }
        if (!active_ || request_generation != generation_ || requested_profile != profile_) {
            return;
        }
        if (reply.isError()) {
            setLastError(tr("Could not load backup target state from the system manager."));
            return;
        }
        applyDeviceState(reply.value());
    });
}

void BackupStatusModel::requestHistory() {
    if (!supports(QStringLiteral("sanitized-history")) || history_request_pending_) {
        return;
    }
    history_request_pending_ = true;
    const quint64 request_generation = generation_;
    const QString requested_profile = profile_;
    auto* watcher = new QDBusPendingCallWatcher(managerCall(bus_, QStringLiteral("GetHistorySanitized"), {requested_profile, 0U, 5U}), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, request_generation, requested_profile](QDBusPendingCallWatcher*) {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        if (request_generation == generation_) {
            history_request_pending_ = false;
        }
        if (!active_ || request_generation != generation_ || requested_profile != profile_) {
            return;
        }
        if (reply.isError()) {
            setLastError(tr("Could not load backup history from the system manager."));
            return;
        }
        applyHistory(reply.value());
    });
}

void BackupStatusModel::requestProfiles() {
    const quint64 request_generation = generation_;
    auto* watcher = new QDBusPendingCallWatcher(managerCall(bus_, QStringLiteral("ListProfiles")), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, request_generation](QDBusPendingCallWatcher*) {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        if (!active_ || request_generation != generation_) {
            return;
        }
        if (reply.isError()) {
            setLastError(tr("Could not load backup profiles from the system manager."));
            return;
        }
        applyProfiles(reply.value());
    });
}

void BackupStatusModel::requestStatus() {
    if (status_request_pending_) {
        return;
    }
    status_request_pending_ = true;
    const quint64 request_generation = generation_;
    const QString requested_profile = profile_;
    auto* watcher = new QDBusPendingCallWatcher(managerCall(bus_, QStringLiteral("GetStatus"), {requested_profile}), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, request_generation, requested_profile](QDBusPendingCallWatcher*) {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        if (request_generation == generation_) {
            status_request_pending_ = false;
        }
        if (!active_ || request_generation != generation_ || requested_profile != profile_) {
            return;
        }
        if (reply.isError()) {
            setLastError(tr("Could not load backup status from the system manager."));
            return;
        }
        applyStatus(reply.value());
    });
}

void BackupStatusModel::applyProfiles(const QString& payload) {
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) {
        setLastError(parseError(error));
        return;
    }

    QString profile_name;
    QVariantList profiles;
    for (const QJsonValue& value : document.array()) {
        const QJsonObject item = value.toObject();
        QVariantMap profile;
        profile.insert(QStringLiteral("profileId"), item.value(QStringLiteral("profileId")).toString());
        profile.insert(QStringLiteral("name"), item.value(QStringLiteral("name")).toString());
        profile.insert(QStringLiteral("targetName"), item.value(QStringLiteral("targetName")).toString());
        profiles.push_back(profile);
        if (item.value(QStringLiteral("profileId")).toString() == profile_) {
            profile_name = item.value(QStringLiteral("name")).toString();
        }
    }
    if (profiles_ != profiles) {
        profiles_ = profiles;
        emit profilesChanged();
    }
    if (profile_name_ != profile_name) {
        profile_name_ = profile_name;
        emit statusChanged();
    }
}

void BackupStatusModel::applyStatus(const QString& payload) {
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        setLastError(parseError(error));
        return;
    }
    const QJsonObject status = document.object();
    if (json_int(status, "schemaVersion", -1) != 3) {
        setLastError(tr("The backup manager returned an unsupported status schema."));
        return;
    }

    const QString previous_state = state_;
    run_id_ = status.value(QStringLiteral("runId")).toString();
    state_ = status.value(QStringLiteral("state")).toString(QStringLiteral("unknown"));
    phase_ = status.value(QStringLiteral("phase")).toString(QStringLiteral("idle"));
    activity_ = status.value(QStringLiteral("activity")).toString(QStringLiteral("idle"));
    can_cancel_ = status.value(QStringLiteral("canCancel")).toBool(false);
    current_source_name_ = status.value(QStringLiteral("sourceName")).toString();
    target_name_ = status.value(QStringLiteral("targetName")).toString();
    speed_bps_ = json_int64(status, "speedBps");
    eta_seconds_ = json_int(status, "etaSeconds", -1);
    source_progress_ = json_int(status, "sourceProgress", -1);
    overall_progress_ = json_int(status, "overallProgress", -1);
    progress_accuracy_ = status.value(QStringLiteral("progressAccuracy")).toString(QStringLiteral("indeterminate"));
    error_code_ = status.value(QStringLiteral("errorCode")).toString();
    setManagerConnected(true);
    setLastError(QString());
    emit statusChanged();
    if (previous_state == QStringLiteral("running") && state_ != previous_state) {
        requestHistory();
    }
}

void BackupStatusModel::applyDeviceState(const QString& payload) {
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        setLastError(parseError(error));
        return;
    }
    const QJsonObject target = document.object();
    if (json_int(target, "schemaVersion", -1) != 1) {
        setLastError(tr("The backup manager returned an unsupported target schema."));
        return;
    }
    target_state_ = target.value(QStringLiteral("state")).toString(QStringLiteral("unknown"));
    target_connected_ = target.value(QStringLiteral("connected")).toBool(false);
    target_unlocked_ = target.value(QStringLiteral("unlocked")).toBool(false);
    target_mounted_ = target.value(QStringLiteral("mounted")).toBool(false);
    safe_to_remove_ = target.value(QStringLiteral("safeToRemove")).toBool(false);
    const QString reported_name = target.value(QStringLiteral("targetName")).toString();
    if (!reported_name.isEmpty()) {
        target_name_ = reported_name;
    }
    emit targetChanged();
    emit statusChanged();
}

void BackupStatusModel::applyHistory(const QString& payload) {
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(payload.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()) {
        setLastError(parseError(error));
        return;
    }
    QVariantList history;
    for (const QJsonValue& value : document.array()) {
        const QJsonObject item = value.toObject();
        QVariantMap entry;
        entry.insert(QStringLiteral("state"), item.value(QStringLiteral("state")).toString());
        entry.insert(QStringLiteral("errorCode"), item.value(QStringLiteral("errorCode")).toString());
        entry.insert(QStringLiteral("sourceName"), item.value(QStringLiteral("sourceName")).toString());
        entry.insert(QStringLiteral("targetName"), item.value(QStringLiteral("targetName")).toString());
        entry.insert(QStringLiteral("finishedAt"), item.value(QStringLiteral("finishedAt")).toString());
        entry.insert(QStringLiteral("overallProgress"), json_int(item, "overallProgress", -1));
        history.push_back(entry);
    }
    history_ = history;
    emit historyChanged();
}

void BackupStatusModel::requestOperation(const QString& method, const QVariantList& arguments) {
    if (!active_ || !capabilities_verified_ || operation_pending_) {
        return;
    }
    const QMap<QString, QString> required_features{
        {QStringLiteral("StartBackup"), QStringLiteral("start-backup")},
        {QStringLiteral("CancelBackup"), QStringLiteral("cancel-backup")},
        {QStringLiteral("ValidateTarget"), QStringLiteral("validate-target")},
        {QStringLiteral("EjectTarget"), QStringLiteral("eject-target")},
    };
    if (!supports(required_features.value(method))) {
        setLastError(tr("The backup manager does not support this operation."));
        return;
    }
    operation_pending_ = true;
    last_operation_.clear();
    setLastError(QString());
    emit operationChanged();
    const quint64 request_generation = generation_;
    auto* watcher = new QDBusPendingCallWatcher(managerCall(bus_, method, arguments), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, request_generation](QDBusPendingCallWatcher*) {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        if (!active_ || request_generation != generation_) {
            return;
        }
        operation_pending_ = false;
        if (reply.isError()) {
            setLastError(tr("The requested backup operation failed: %1").arg(reply.error().message()));
            emit operationChanged();
            return;
        }
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(reply.value().toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject() ||
            json_int(document.object(), "schemaVersion", -1) != 1) {
            setLastError(parseError(error));
            emit operationChanged();
            return;
        }
        last_operation_ = document.object().value(QStringLiteral("operation")).toString();
        emit operationChanged();
        requestStatus();
        requestDeviceState();
        requestHistory();
    });
}

bool BackupStatusModel::supports(const QString& feature) const {
    return features_.contains(feature);
}

void BackupStatusModel::setManagerConnected(bool connected) {
    if (manager_connected_ == connected) {
        return;
    }
    manager_connected_ = connected;
    emit managerConnectedChanged();
}

void BackupStatusModel::setLastError(const QString& message) {
    if (last_error_ == message) {
        return;
    }
    last_error_ = message;
    emit errorChanged();
}

void BackupStatusModel::managerUnavailable() {
    poll_timer_.stop();
    ++generation_;
    capabilities_verified_ = false;
    status_request_pending_ = false;
    device_request_pending_ = false;
    history_request_pending_ = false;
    features_.clear();
    operation_pending_ = false;
    emit operationChanged();
    setManagerConnected(false);
    if (last_error_.isEmpty()) {
        setLastError(tr("The system backup manager is unavailable."));
    }
}
