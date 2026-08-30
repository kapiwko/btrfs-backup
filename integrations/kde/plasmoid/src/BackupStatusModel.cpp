// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BackupStatusModel.hpp"

#include "ManagerApi.hpp"

#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMap>
#include <QProcess>
#include <QUrl>

#include <utility>

namespace {

constexpr int operation_message_timeout_ms = 5000;

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

} // namespace

BackupStatusModel::BackupStatusModel(QObject* parent)
    : QObject(parent),
      bus_(QDBusConnection::systemBus()),
      manager_events_(bus_, this),
      service_watcher_(QLatin1String(btrfsbackup::manager_protocol::service_name), bus_, QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this) {
    connect(&run_, &RunStatusModel::changed, this, &BackupStatusModel::statusChanged);
    connect(&run_, &RunStatusModel::activeRunFinished, this, [this]() {
        requestDeviceState();
        requestHistory();
    });
    connect(&target_, &TargetStatusModel::changed, this, &BackupStatusModel::targetChanged);
    connect(&history_, &BackupHistoryModel::changed, this, &BackupStatusModel::historyChanged);
    operation_message_timer_.setInterval(operation_message_timeout_ms);
    operation_message_timer_.setSingleShot(true);
    connect(&operation_message_timer_, &QTimer::timeout, this, [this]() {
        if (!last_operation_.isEmpty()) {
            last_operation_.clear();
            emit operationChanged();
        }
    });
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
    connect(&manager_events_, &btrfsbackup::kde::ManagerEventSubscriber::profilesChanged, this, [this]() {
        if (active_ && capabilities_verified_) {
            requestProfiles();
            requestStatus();
            requestDeviceState();
            requestHistory();
        }
    });
    connect(&manager_events_, &btrfsbackup::kde::ManagerEventSubscriber::statusChanged, this, [this](const QString& profile_id) {
        if (active_ && capabilities_verified_ && profile_id == profile_)
            requestStatus();
    });
    connect(&manager_events_, &btrfsbackup::kde::ManagerEventSubscriber::historyChanged, this, [this](const QString& profile_id) {
        if (active_ && capabilities_verified_ && profile_id == profile_) {
            requestStatus();
            requestHistory();
        }
    });
    connect(&manager_events_, &btrfsbackup::kde::ManagerEventSubscriber::deviceStateChanged, this, [this](const QString& profile_id) {
        if (active_ && capabilities_verified_ && profile_id == profile_)
            requestDeviceState();
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
    run_.reset();
    target_.reset();
    history_.reset();
    operation_message_timer_.stop();
    last_operation_.clear();
    ++generation_;
    profiles_request_pending_ = false;
    status_request_pending_ = false;
    device_request_pending_ = false;
    history_request_pending_ = false;
    profiles_refresh_queued_ = false;
    status_refresh_queued_ = false;
    device_refresh_queued_ = false;
    history_refresh_queued_ = false;
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

RunStatusModel* BackupStatusModel::run() {
    return &run_;
}

TargetStatusModel* BackupStatusModel::target() {
    return &target_;
}

BackupHistoryModel* BackupStatusModel::history() {
    return &history_;
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

bool BackupStatusModel::browseSupported() const {
    return supports(QLatin1String(btrfsbackup::manager_protocol::feature::browse_backups));
}

void BackupStatusModel::start() {
    active_ = true;
    setLastError(QString());
    connectToManager();
}

void BackupStatusModel::stop() {
    active_ = false;
    ++generation_;
    profiles_request_pending_ = false;
    status_request_pending_ = false;
    device_request_pending_ = false;
    history_request_pending_ = false;
    profiles_refresh_queued_ = false;
    status_refresh_queued_ = false;
    device_refresh_queued_ = false;
    history_refresh_queued_ = false;
    capabilities_verified_ = false;
    operation_pending_ = false;
    operation_message_timer_.stop();
    last_operation_.clear();
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
    requestOperation(QLatin1String(btrfsbackup::manager_protocol::method::start_backup), {profile_});
}

void BackupStatusModel::cancelBackup() {
    if (run_.runId().isEmpty()) {
        setLastError(tr("No active backup run can be cancelled."));
        return;
    }
    requestOperation(QLatin1String(btrfsbackup::manager_protocol::method::cancel_backup), {profile_, run_.runId()});
}

void BackupStatusModel::validateTarget() {
    requestOperation(QLatin1String(btrfsbackup::manager_protocol::method::validate_target), {profile_});
}

void BackupStatusModel::ejectTarget() {
    requestOperation(QLatin1String(btrfsbackup::manager_protocol::method::eject_target), {profile_});
}

void BackupStatusModel::openSettings() {
    if (!QProcess::startDetached(QStringLiteral("systemsettings"), {QStringLiteral("kcm_btrfsbackup")})) {
        setLastError(tr("Could not open backup settings."));
    }
}

void BackupStatusModel::browseBackups() {
    if (!browseSupported()) {
        return;
    }
    QUrl location;
    location.setScheme(QStringLiteral("btrfsbackup"));
    location.setPath(QStringLiteral("/profiles/") + profile_);
    if (!QProcess::startDetached(QStringLiteral("dolphin"), {location.toString()})) {
        setLastError(tr("Could not open backup snapshots."));
    }
}

void BackupStatusModel::connectToManager() {
    capabilities_verified_ = false;
    profiles_request_pending_ = false;
    status_request_pending_ = false;
    device_request_pending_ = false;
    history_request_pending_ = false;
    profiles_refresh_queued_ = false;
    status_refresh_queued_ = false;
    device_refresh_queued_ = false;
    history_refresh_queued_ = false;
    const quint64 request_generation = ++generation_;
    auto* watcher = new QDBusPendingCallWatcher(btrfsbackup::kde::manager_call(bus_, QLatin1String(btrfsbackup::manager_protocol::method::get_capabilities)), this);
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

        const auto capabilities = btrfsbackup::kde::parse_capabilities(reply.value());
        if (!capabilities.has_value()) {
            setLastError(tr("Invalid manager response."));
            managerUnavailable();
            return;
        }
        if (capabilities->api_major != btrfsbackup::manager_protocol::api_major ||
            capabilities->public_status_schema_version != btrfsbackup::manager_protocol::public_status_schema_version ||
            capabilities->history_schema_version != btrfsbackup::manager_protocol::history_schema_version ||
            !capabilities->features.contains(QLatin1String(btrfsbackup::manager_protocol::feature::change_signals))) {
            setLastError(tr("The backup manager API is not compatible with this widget."));
            managerUnavailable();
            return;
        }

        features_ = capabilities->features;
        run_.setCancelSupported(supports(
            QLatin1String(btrfsbackup::manager_protocol::feature::cancel_backup)
        ));
        target_.setStorageSupported(supports(
            QLatin1String(btrfsbackup::manager_protocol::feature::target_storage_usage)
        ));

        capabilities_verified_ = true;
        setManagerConnected(true);
        setLastError(QString());
        requestProfiles();
        requestStatus();
        requestDeviceState();
        requestHistory();
    });
}

void BackupStatusModel::requestDeviceState() {
    if (!supports(QLatin1String(btrfsbackup::manager_protocol::feature::device_state))) {
        return;
    }
    if (device_request_pending_) {
        device_refresh_queued_ = true;
        return;
    }
    device_request_pending_ = true;
    const quint64 request_generation = generation_;
    const QString requested_profile = profile_;
    auto* watcher = new QDBusPendingCallWatcher(btrfsbackup::kde::manager_call(bus_, QLatin1String(btrfsbackup::manager_protocol::method::get_device_state), {requested_profile}), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, request_generation, requested_profile](QDBusPendingCallWatcher*) {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        if (!active_ || request_generation != generation_ || requested_profile != profile_) {
            return;
        }
        device_request_pending_ = false;
        const bool refresh_again = std::exchange(device_refresh_queued_, false);
        if (reply.isError()) {
            setLastError(tr("Could not load backup target state from the system manager."));
        } else {
            applyDeviceState(reply.value());
        }
        if (refresh_again)
            requestDeviceState();
    });
}

void BackupStatusModel::requestHistory() {
    if (!supports(QLatin1String(btrfsbackup::manager_protocol::feature::sanitized_history))) {
        return;
    }
    if (history_request_pending_) {
        history_refresh_queued_ = true;
        return;
    }
    history_request_pending_ = true;
    const quint64 request_generation = generation_;
    const QString requested_profile = profile_;
    auto* watcher = new QDBusPendingCallWatcher(btrfsbackup::kde::manager_call(bus_, QLatin1String(btrfsbackup::manager_protocol::method::get_history_sanitized), {requested_profile, 0U, 3U}), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, request_generation, requested_profile](QDBusPendingCallWatcher*) {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        if (!active_ || request_generation != generation_ || requested_profile != profile_) {
            return;
        }
        history_request_pending_ = false;
        const bool refresh_again = std::exchange(history_refresh_queued_, false);
        if (reply.isError()) {
            setLastError(tr("Could not load backup history from the system manager."));
        } else {
            applyHistory(reply.value());
        }
        if (refresh_again)
            requestHistory();
    });
}

void BackupStatusModel::requestProfiles() {
    if (profiles_request_pending_) {
        profiles_refresh_queued_ = true;
        return;
    }
    profiles_request_pending_ = true;
    const quint64 request_generation = generation_;
    auto* watcher = new QDBusPendingCallWatcher(btrfsbackup::kde::manager_call(bus_, QLatin1String(btrfsbackup::manager_protocol::method::list_profiles)), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, request_generation](QDBusPendingCallWatcher*) {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        if (!active_ || request_generation != generation_) {
            return;
        }
        profiles_request_pending_ = false;
        const bool refresh_again = std::exchange(profiles_refresh_queued_, false);
        if (reply.isError()) {
            setLastError(tr("Could not load backup profiles from the system manager."));
        } else {
            applyProfiles(reply.value());
        }
        if (refresh_again)
            requestProfiles();
    });
}

void BackupStatusModel::requestStatus() {
    if (status_request_pending_) {
        status_refresh_queued_ = true;
        return;
    }
    status_request_pending_ = true;
    const quint64 request_generation = generation_;
    const QString requested_profile = profile_;
    auto* watcher = new QDBusPendingCallWatcher(btrfsbackup::kde::manager_call(bus_, QLatin1String(btrfsbackup::manager_protocol::method::get_status), {requested_profile}), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, request_generation, requested_profile](QDBusPendingCallWatcher*) {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        if (!active_ || request_generation != generation_ || requested_profile != profile_) {
            return;
        }
        status_request_pending_ = false;
        const bool refresh_again = std::exchange(status_refresh_queued_, false);
        if (reply.isError()) {
            setLastError(tr("Could not load backup status from the system manager."));
        } else {
            applyStatus(reply.value());
        }
        if (refresh_again)
            requestStatus();
    });
}

void BackupStatusModel::applyProfiles(const QString& payload) {
    const auto decoded_profiles = btrfsbackup::kde::parse_profiles(payload);
    if (!decoded_profiles.has_value()) {
        setLastError(tr("Invalid manager response."));
        return;
    }

    QString profile_name;
    QVariantList profiles;
    for (const auto& decoded : *decoded_profiles) {
        QVariantMap profile;
        profile.insert(QStringLiteral("profileId"), decoded.id);
        profile.insert(QStringLiteral("name"), decoded.name);
        profile.insert(QStringLiteral("targetName"), decoded.target_name);
        profiles.push_back(profile);
        if (decoded.id == profile_) {
            profile_name = decoded.name;
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
    if (!run_.apply(payload)) {
        setLastError(tr("The backup manager returned an unsupported status schema."));
        return;
    }
    setManagerConnected(true);
    setLastError(QString());
}

void BackupStatusModel::applyDeviceState(const QString& payload) {
    if (!target_.apply(profile_, payload)) {
        setLastError(tr("The backup manager returned an unsupported target schema."));
        return;
    }
    setLastError(QString());
}

void BackupStatusModel::applyHistory(const QString& payload) {
    if (!history_.apply(payload)) {
        setLastError(tr("Invalid manager response."));
        return;
    }
}

void BackupStatusModel::requestOperation(const QString& method, const QVariantList& arguments) {
    if (!active_ || !capabilities_verified_ || operation_pending_) {
        return;
    }
    const QMap<QString, QString> required_features{
        {QLatin1String(btrfsbackup::manager_protocol::method::start_backup), QLatin1String(btrfsbackup::manager_protocol::feature::start_backup)},
        {QLatin1String(btrfsbackup::manager_protocol::method::cancel_backup), QLatin1String(btrfsbackup::manager_protocol::feature::cancel_backup)},
        {QLatin1String(btrfsbackup::manager_protocol::method::validate_target), QLatin1String(btrfsbackup::manager_protocol::feature::validate_target)},
        {QLatin1String(btrfsbackup::manager_protocol::method::eject_target), QLatin1String(btrfsbackup::manager_protocol::feature::eject_target)},
    };
    if (!supports(required_features.value(method))) {
        setLastError(tr("The backup manager does not support this operation."));
        return;
    }
    operation_pending_ = true;
    operation_message_timer_.stop();
    last_operation_.clear();
    setLastError(QString());
    emit operationChanged();
    const quint64 request_generation = generation_;
    auto* watcher = new QDBusPendingCallWatcher(btrfsbackup::kde::manager_call(bus_, method, arguments), this);
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
        operation_message_timer_.start();
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
    ++generation_;
    capabilities_verified_ = false;
    profiles_request_pending_ = false;
    status_request_pending_ = false;
    device_request_pending_ = false;
    history_request_pending_ = false;
    profiles_refresh_queued_ = false;
    status_refresh_queued_ = false;
    device_refresh_queued_ = false;
    history_refresh_queued_ = false;
    features_.clear();
    run_.setCancelSupported(false);
    target_.setStorageSupported(false);
    target_.reset();
    operation_pending_ = false;
    emit operationChanged();
    setManagerConnected(false);
    if (last_error_.isEmpty()) {
        setLastError(tr("The system backup manager is unavailable."));
    }
}
