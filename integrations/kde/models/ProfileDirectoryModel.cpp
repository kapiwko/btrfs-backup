// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ProfileDirectoryModel.hpp"

#include "DesktopLauncher.hpp"

#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>

#include <utility>

ProfileDirectoryModel::ProfileDirectoryModel(QObject* parent)
    : QObject(parent),
      bus_(QDBusConnection::systemBus()),
      manager_events_(bus_, this),
      service_watcher_(QLatin1String(btrfsbackup::manager_protocol::service_name), bus_, QDBusServiceWatcher::WatchForRegistration | QDBusServiceWatcher::WatchForUnregistration, this) {
    connect(&service_watcher_, &QDBusServiceWatcher::serviceRegistered, this, [this]() {
        if (active_)
            connectToManager();
    });
    connect(&service_watcher_, &QDBusServiceWatcher::serviceUnregistered, this, [this]() {
        if (active_)
            managerUnavailable();
    });
    connect(&manager_events_, &btrfsbackup::kde::ManagerEventSubscriber::profilesChanged, this, [this]() {
        if (!active_ || !capabilities_verified_)
            return;
        requestProfiles();
        emit refreshRequested();
    });
    connect(&manager_events_, &btrfsbackup::kde::ManagerEventSubscriber::statusChanged, this, &ProfileDirectoryModel::profileStatusInvalidated);
    connect(&manager_events_, &btrfsbackup::kde::ManagerEventSubscriber::historyChanged, this, &ProfileDirectoryModel::profileHistoryInvalidated);
    connect(&manager_events_, &btrfsbackup::kde::ManagerEventSubscriber::deviceStateChanged, this, [this](const QString& profile_id) {
        emit profileDeviceStateInvalidated(profile_id);
        if (active_ && capabilities_verified_)
            requestProfiles();
    });
}

QVariantList ProfileDirectoryModel::profiles() const { return profiles_; }
bool ProfileDirectoryModel::managerConnected() const { return manager_connected_; }
QString ProfileDirectoryModel::lastError() const { return last_error_; }
QString ProfileDirectoryModel::lastErrorCode() const { return last_error_code_; }
bool ProfileDirectoryModel::supports(const QString& feature) const { return features_.contains(feature); }

void ProfileDirectoryModel::start() {
    if (active_)
        return;
    active_ = true;
    setLastError({});
    connectToManager();
}

void ProfileDirectoryModel::stop() {
    active_ = false;
    ++generation_;
    capabilities_verified_ = false;
    profiles_request_pending_ = false;
    profiles_refresh_queued_ = false;
    features_.clear();
    setManagerConnected(false);
}

void ProfileDirectoryModel::refreshNow() {
    if (!active_) {
        start();
        return;
    }
    if (!capabilities_verified_) {
        connectToManager();
        return;
    }
    requestProfiles();
    emit refreshRequested();
}

void ProfileDirectoryModel::openSettings() {
    btrfsbackup::kde::launcher::launch(
        btrfsbackup::kde::launcher::open_backup_settings(), this,
        [this](const QString&) { setLastError(tr("Could not open backup settings."), QStringLiteral("desktop.settings-launch-failed")); }
    );
}

void ProfileDirectoryModel::openNotificationSettings() {
    btrfsbackup::kde::launcher::launch(
        btrfsbackup::kde::launcher::open_notification_settings(), this,
        [this](const QString&) { setLastError(tr("Could not open notification settings."), QStringLiteral("desktop.notification-settings-launch-failed")); }
    );
}

void ProfileDirectoryModel::connectToManager() {
    capabilities_verified_ = false;
    profiles_request_pending_ = false;
    profiles_refresh_queued_ = false;
    const quint64 request_generation = ++generation_;
    auto* watcher = new QDBusPendingCallWatcher(btrfsbackup::kde::ManagerClient{bus_}.capabilities(), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, request_generation](QDBusPendingCallWatcher*) {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        if (!active_ || request_generation != generation_)
            return;
        if (reply.isError()) {
            managerUnavailable();
            return;
        }
        const auto capabilities = btrfsbackup::kde::parse_capabilities(reply.value());
        if (!capabilities.has_value()) {
            setLastError(tr("Invalid manager response."), QStringLiteral("manager.invalid-response"));
            managerUnavailable();
            return;
        }
        if (capabilities->api_major != btrfsbackup::manager_protocol::api_major ||
            capabilities->public_status_schema_version != btrfsbackup::manager_protocol::public_status_schema_version ||
            capabilities->history_schema_version != btrfsbackup::manager_protocol::history_schema_version ||
            !capabilities->features.contains(QLatin1String(btrfsbackup::manager_protocol::feature::change_signals))) {
            setLastError(tr("The backup manager API is not compatible with this interface."), QStringLiteral("manager.incompatible-api"));
            managerUnavailable();
            return;
        }
        features_ = capabilities->features;
        capabilities_verified_ = true;
        setManagerConnected(true);
        setLastError({});
        requestProfiles();
    });
}

void ProfileDirectoryModel::requestProfiles() {
    if (profiles_request_pending_) {
        profiles_refresh_queued_ = true;
        return;
    }
    profiles_request_pending_ = true;
    const quint64 request_generation = generation_;
    auto* watcher = new QDBusPendingCallWatcher(btrfsbackup::kde::ManagerClient{bus_}.profiles(), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, request_generation](QDBusPendingCallWatcher*) {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        if (!active_ || request_generation != generation_)
            return;
        profiles_request_pending_ = false;
        const bool refresh_again = std::exchange(profiles_refresh_queued_, false);
        if (reply.isError())
            setLastError(tr("Could not load backup profiles from the system manager."), reply.error().name());
        else
            applyProfiles(reply.value());
        if (refresh_again)
            requestProfiles();
    });
}

void ProfileDirectoryModel::applyProfiles(const QString& payload) {
    const auto decoded_profiles = btrfsbackup::kde::parse_profiles(payload);
    if (!decoded_profiles.has_value()) {
        setLastError(tr("Invalid manager response."), QStringLiteral("manager.invalid-response"));
        return;
    }
    QVariantList profiles;
    profiles.reserve(decoded_profiles->size());
    for (const auto& decoded : *decoded_profiles) {
        QVariantMap profile;
        profile.insert(QStringLiteral("profileId"), decoded.id);
        profile.insert(QStringLiteral("name"), decoded.name);
        profile.insert(QStringLiteral("enabled"), decoded.enabled);
        profile.insert(QStringLiteral("targetName"), decoded.target_name);
        profile.insert(QStringLiteral("configurationValid"), decoded.configuration_valid);
        profile.insert(QStringLiteral("configurationErrorCode"), decoded.configuration_error_code);
        profiles.push_back(profile);
    }
    if (profiles_ != profiles) {
        profiles_ = profiles;
        emit profilesChanged();
    }
    setLastError({});
}

void ProfileDirectoryModel::setManagerConnected(bool connected) {
    if (manager_connected_ == connected)
        return;
    manager_connected_ = connected;
    emit managerConnectedChanged();
}

void ProfileDirectoryModel::setLastError(const QString& message, const QString& code) {
    if (last_error_ == message && last_error_code_ == code)
        return;
    last_error_ = message;
    last_error_code_ = code;
    emit errorChanged();
}

void ProfileDirectoryModel::managerUnavailable() {
    ++generation_;
    capabilities_verified_ = false;
    profiles_request_pending_ = false;
    profiles_refresh_queued_ = false;
    features_.clear();
    setManagerConnected(false);
    if (last_error_.isEmpty())
        setLastError(tr("The system backup manager is unavailable."), QStringLiteral("manager.unavailable"));
}
