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
    ++generation_;
    status_request_pending_ = false;
    emit profileChanged();
    emit statusChanged();
    if (active_ && capabilities_verified_) {
        requestProfiles();
        requestStatus();
    } else if (active_) {
        connectToManager();
    }
}

bool BackupStatusModel::managerConnected() const {
    return manager_connected_;
}

QString BackupStatusModel::profileName() const {
    return profile_name_;
}

QString BackupStatusModel::state() const {
    return state_;
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
    capabilities_verified_ = false;
    setManagerConnected(false);
}

void BackupStatusModel::connectToManager() {
    poll_timer_.stop();
    capabilities_verified_ = false;
    status_request_pending_ = false;
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

        capabilities_verified_ = true;
        setManagerConnected(true);
        setLastError(QString());
        requestProfiles();
        requestStatus();
        poll_timer_.start();
    });
}

void BackupStatusModel::refresh() {
    if (active_ && capabilities_verified_) {
        requestStatus();
    }
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
    for (const QJsonValue& value : document.array()) {
        const QJsonObject item = value.toObject();
        if (item.value(QStringLiteral("profileId")).toString() == profile_) {
            profile_name = item.value(QStringLiteral("name")).toString();
            break;
        }
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

    state_ = status.value(QStringLiteral("state")).toString(QStringLiteral("unknown"));
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
    setManagerConnected(false);
    if (last_error_.isEmpty()) {
        setLastError(tr("The system backup manager is unavailable."));
    }
}
