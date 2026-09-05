// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BackupStatusModel.hpp"

#include "DesktopLauncher.hpp"
#include "ManagerApi.hpp"

#include <KLocalizedString>
#include <QDBusError>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QMap>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace {

constexpr int operation_message_timeout_ms = 5000;
constexpr auto translation_domain = "plasma_applet_org.btrfsbackup.plasmoid";

QString manager_error_message(const QDBusError& error) {
    const QString name = error.name();
    if (name.endsWith(QStringLiteral(".InvalidRequest")))
        return i18nd(translation_domain, "The request is invalid.");
    if (name.endsWith(QStringLiteral(".SourceMissing")))
        return i18nd(translation_domain, "The selected backup source does not exist.");
    if (name.endsWith(QStringLiteral(".SourceNotSubvolume")))
        return i18nd(translation_domain, "The selected backup source is not a Btrfs subvolume.");
    if (name.endsWith(QStringLiteral(".SourceUnavailable")))
        return i18nd(translation_domain, "The selected backup source cannot be inspected.");
    if (name.endsWith(QStringLiteral(".NotFound")))
        return i18nd(translation_domain, "The requested item was not found.");
    if (name.endsWith(QStringLiteral(".NotAuthorized")))
        return i18nd(translation_domain, "The operation was cancelled or you do not have permission to perform it.");
    if (name.endsWith(QStringLiteral(".Busy")))
        return i18nd(translation_domain, "The requested item is busy.");
    if (name.endsWith(QStringLiteral(".RunMismatch")))
        return i18nd(translation_domain, "The active backup run has changed. Refresh and try again.");
    if (name.endsWith(QStringLiteral(".TargetUnavailable")))
        return i18nd(translation_domain, "The backup device is disconnected or unavailable.");
    if (name.endsWith(QStringLiteral(".Conflict")))
        return i18nd(translation_domain, "The operation conflicts with the current state. Refresh and try again.");
    if (name.endsWith(QStringLiteral(".SaveFailed")))
        return i18nd(translation_domain, "The configuration could not be saved.");
    if (name.endsWith(QStringLiteral(".RollbackIncomplete")))
        return i18nd(translation_domain, "The operation could not be fully rolled back. Review the system log.");
    return i18nd(translation_domain, "The backup manager could not complete the request.");
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
    connect(&history_, &BackupHistoryModel::stateChanged, this, [this]() {
        if (!history_.errorCode().isEmpty())
            setLastError(tr("Could not load backup history from the system manager."), history_.errorCode());
    });
    history_.setPageSize(history_limit_);
    operation_message_timer_.setInterval(operation_message_timeout_ms);
    operation_message_timer_.setSingleShot(true);
    connect(&operation_message_timer_, &QTimer::timeout, this, [this]() {
        if (!last_operation_.isEmpty()) {
            last_operation_.clear();
            emit operationChanged();
        }
    });
    connect(&service_watcher_, &QDBusServiceWatcher::serviceRegistered, this, [this]() {
        if (active_ && shared_source_ == nullptr) {
            connectToManager();
        }
    });
    connect(&service_watcher_, &QDBusServiceWatcher::serviceUnregistered, this, [this]() {
        if (active_ && shared_source_ == nullptr) {
            managerUnavailable();
        }
    });
    connect(&manager_events_, &btrfsbackup::kde::ManagerEventSubscriber::profilesChanged, this, [this]() {
        if (active_ && capabilities_verified_ && shared_source_ == nullptr) {
            requestProfiles();
            if (!directory_only_) {
                requestStatus();
                requestDeviceState();
                requestHistory();
            }
        }
    });
    connect(&manager_events_, &btrfsbackup::kde::ManagerEventSubscriber::statusChanged, this, [this](const QString& profile_id) {
        if (shared_source_ == nullptr)
            emit profileStatusInvalidated(profile_id);
        if (active_ && capabilities_verified_ && shared_source_ == nullptr && !directory_only_ && profile_id == profile_)
            requestStatus();
    });
    connect(&manager_events_, &btrfsbackup::kde::ManagerEventSubscriber::historyChanged, this, [this](const QString& profile_id) {
        if (shared_source_ == nullptr)
            emit profileHistoryInvalidated(profile_id);
        if (active_ && capabilities_verified_ && shared_source_ == nullptr && !directory_only_ && profile_id == profile_)
            requestStatus();
    });
    connect(&manager_events_, &btrfsbackup::kde::ManagerEventSubscriber::deviceStateChanged, this, [this](const QString& profile_id) {
        if (shared_source_ == nullptr)
            emit profileDeviceStateInvalidated(profile_id);
        if (active_ && capabilities_verified_ && shared_source_ == nullptr && !directory_only_ && profile_id == profile_) {
            requestDeviceState();
            requestProfiles();
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
    profile_enabled_ = true;
    run_.reset();
    target_.reset();
    if (active_)
        history_.setProfileId(profile_);
    else
        history_.reset();
    operation_message_timer_.stop();
    last_operation_.clear();
    ++generation_;
    profiles_request_pending_ = false;
    status_request_pending_ = false;
    device_request_pending_ = false;
    profiles_refresh_queued_ = false;
    status_refresh_queued_ = false;
    device_refresh_queued_ = false;
    operation_pending_ = false;
    emit profileChanged();
    emit statusChanged();
    emit targetChanged();
    emit historyChanged();
    emit operationChanged();
    if (active_ && capabilities_verified_) {
        if (shared_source_ != nullptr) {
            syncFromSharedSource();
        } else {
            requestProfiles();
            requestStatus();
            requestDeviceState();
        }
    } else if (active_) {
        if (shared_source_ != nullptr)
            syncFromSharedSource();
        else
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

bool BackupStatusModel::profileEnabled() const {
    return profile_enabled_;
}
bool BackupStatusModel::configurationValid() const {
    return configuration_valid_;
}
QString BackupStatusModel::configurationErrorCode() const {
    return configuration_error_code_;
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

QString BackupStatusModel::lastErrorCode() const {
    return last_error_code_;
}

bool BackupStatusModel::browseSupported() const {
    return supports(QLatin1String(btrfsbackup::manager_protocol::feature::browse_backups));
}

int BackupStatusModel::historyLimit() const {
    return history_limit_;
}

void BackupStatusModel::setHistoryLimit(int limit) {
    const int bounded = std::clamp(limit, 1, 10);
    if (history_limit_ == bounded) {
        return;
    }
    history_limit_ = bounded;
    history_.setPageSize(history_limit_);
    emit historyLimitChanged();
}

BackupStatusModel* BackupStatusModel::sharedSource() const {
    return shared_source_;
}

void BackupStatusModel::setSharedSource(BackupStatusModel* source) {
    if (shared_source_ == source || source == this)
        return;
    if (shared_source_ != nullptr)
        disconnect(shared_source_, nullptr, this, nullptr);
    shared_source_ = source;
    if (shared_source_ != nullptr) {
        connect(shared_source_, &BackupStatusModel::managerConnectedChanged, this, &BackupStatusModel::syncFromSharedSource);
        connect(shared_source_, &BackupStatusModel::profilesChanged, this, &BackupStatusModel::syncFromSharedSource);
        connect(shared_source_, &BackupStatusModel::profileStatusInvalidated, this, [this](const QString& profile_id) {
            if (active_ && capabilities_verified_ && profile_id == profile_)
                requestStatus();
        });
        connect(shared_source_, &BackupStatusModel::profileHistoryInvalidated, this, [this](const QString& profile_id) {
            if (active_ && capabilities_verified_ && profile_id == profile_) {
                requestStatus();
                requestHistory();
            }
        });
        connect(shared_source_, &BackupStatusModel::profileDeviceStateInvalidated, this, [this](const QString& profile_id) {
            if (active_ && capabilities_verified_ && profile_id == profile_)
                requestDeviceState();
        });
        connect(shared_source_, &BackupStatusModel::sharedRefreshRequested, this, [this]() {
            if (active_ && capabilities_verified_) {
                requestStatus();
                requestDeviceState();
                requestHistory();
            }
        });
    }
    ++generation_;
    capabilities_verified_ = false;
    emit sharedSourceChanged();
    if (active_) {
        if (shared_source_ != nullptr)
            syncFromSharedSource();
        else
            connectToManager();
    }
}

bool BackupStatusModel::directoryOnly() const {
    return directory_only_;
}

void BackupStatusModel::setDirectoryOnly(bool directory_only) {
    if (directory_only_ == directory_only)
        return;
    directory_only_ = directory_only;
    emit directoryOnlyChanged();
}

void BackupStatusModel::start() {
    active_ = true;
    history_.setProfileId(profile_);
    setLastError(QString());
    if (shared_source_ != nullptr)
        syncFromSharedSource();
    else
        connectToManager();
}

void BackupStatusModel::stop() {
    active_ = false;
    history_.setProfileId({});
    ++generation_;
    profiles_request_pending_ = false;
    status_request_pending_ = false;
    device_request_pending_ = false;
    profiles_refresh_queued_ = false;
    status_refresh_queued_ = false;
    device_refresh_queued_ = false;
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
        if (shared_source_ != nullptr)
            syncFromSharedSource();
        else
            connectToManager();
        return;
    }
    if (shared_source_ == nullptr)
        requestProfiles();
    if (directory_only_) {
        emit sharedRefreshRequested();
    } else {
        requestStatus();
        requestDeviceState();
        requestHistory();
    }
}

void BackupStatusModel::startBackup() {
    requestOperation(QLatin1String(btrfsbackup::manager_protocol::method::start_backup), {profile_});
}

void BackupStatusModel::cancelBackup() {
    if (run_.runId().isEmpty()) {
        setLastError(tr("No active backup run can be cancelled."), QStringLiteral("run.not-active"));
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

void BackupStatusModel::setProfileEnabled(bool enabled) {
    requestOperation(
        QLatin1String(btrfsbackup::manager_protocol::method::set_profile_enabled),
        {profile_, enabled}
    );
}

void BackupStatusModel::openSettings() {
    btrfsbackup::kde::launcher::launch(
        btrfsbackup::kde::launcher::open_backup_settings(),
        this,
        [this](const QString&) {
            setLastError(tr("Could not open backup settings."), QStringLiteral("desktop.settings-launch-failed"));
        }
    );
}

void BackupStatusModel::browseBackups() {
    if (!browseSupported() || !target_.connected()) {
        return;
    }
    QUrl location;
    location.setScheme(QStringLiteral("btrfsbackup"));
    location.setPath(QStringLiteral("/") + profile_);
    btrfsbackup::kde::launcher::launch(
        btrfsbackup::kde::launcher::open_backup_directory(std::move(location)),
        this,
        [this](const QString&) {
            setLastError(tr("Could not open backup snapshots."), QStringLiteral("desktop.browser-launch-failed"));
        }
    );
}

void BackupStatusModel::openNotificationSettings() {
    btrfsbackup::kde::launcher::launch(
        btrfsbackup::kde::launcher::open_notification_settings(),
        this,
        [this](const QString&) {
            setLastError(
                tr("Could not open notification settings."),
                QStringLiteral("desktop.notification-settings-launch-failed")
            );
        }
    );
}

void BackupStatusModel::connectToManager() {
    capabilities_verified_ = false;
    profiles_request_pending_ = false;
    status_request_pending_ = false;
    device_request_pending_ = false;
    profiles_refresh_queued_ = false;
    status_refresh_queued_ = false;
    device_refresh_queued_ = false;
    const quint64 request_generation = ++generation_;
    auto* watcher = new QDBusPendingCallWatcher(btrfsbackup::kde::ManagerClient{bus_}.capabilities(), this);
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
            setLastError(tr("Invalid manager response."), QStringLiteral("manager.invalid-response"));
            managerUnavailable();
            return;
        }
        if (capabilities->api_major != btrfsbackup::manager_protocol::api_major ||
            capabilities->public_status_schema_version != btrfsbackup::manager_protocol::public_status_schema_version ||
            capabilities->history_schema_version != btrfsbackup::manager_protocol::history_schema_version ||
            !capabilities->features.contains(QLatin1String(btrfsbackup::manager_protocol::feature::change_signals))) {
            setLastError(tr("The backup manager API is not compatible with this widget."), QStringLiteral("manager.incompatible-api"));
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
        if (!directory_only_) {
            requestStatus();
            requestDeviceState();
            requestHistory();
        }
    });
}

void BackupStatusModel::syncFromSharedSource() {
    if (!active_ || shared_source_ == nullptr)
        return;
    if (!shared_source_->manager_connected_ || !shared_source_->capabilities_verified_) {
        managerUnavailable();
        return;
    }
    features_ = shared_source_->features_;
    capabilities_verified_ = true;
    setManagerConnected(true);
    setLastError(QString());

    profiles_ = shared_source_->profiles_;
    emit profilesChanged();
    QString profile_name;
    bool profile_enabled = true;
    bool configuration_valid = true;
    QString configuration_error_code;
    for (const QVariant& value : profiles_) {
        const QVariantMap decoded = value.toMap();
        if (decoded.value(QStringLiteral("profileId")).toString() == profile_) {
            profile_name = decoded.value(QStringLiteral("name")).toString();
            profile_enabled = decoded.value(QStringLiteral("enabled")).toBool();
            configuration_valid = decoded.value(QStringLiteral("configurationValid"), true).toBool();
            configuration_error_code = decoded.value(QStringLiteral("configurationErrorCode")).toString();
            break;
        }
    }
    profile_name_ = profile_name;
    profile_enabled_ = profile_enabled;
    configuration_valid_ = configuration_valid;
    configuration_error_code_ = configuration_error_code;
    run_.setCancelSupported(supports(QLatin1String(btrfsbackup::manager_protocol::feature::cancel_backup)));
    target_.setStorageSupported(supports(QLatin1String(btrfsbackup::manager_protocol::feature::target_storage_usage)));
    emit statusChanged();
    requestStatus();
    requestDeviceState();
    requestHistory();
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
    auto* watcher = new QDBusPendingCallWatcher(
        btrfsbackup::kde::ManagerClient{bus_}.deviceState(requested_profile),
        this
    );
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, request_generation, requested_profile](QDBusPendingCallWatcher*) {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        if (!active_ || request_generation != generation_ || requested_profile != profile_) {
            return;
        }
        device_request_pending_ = false;
        const bool refresh_again = std::exchange(device_refresh_queued_, false);
        if (reply.isError()) {
            setLastError(tr("Could not load backup target state from the system manager."), reply.error().name());
        } else {
            applyDeviceState(reply.value());
        }
        if (refresh_again)
            requestDeviceState();
    });
}

void BackupStatusModel::requestHistory() {
    history_.loadFirstPage();
}

void BackupStatusModel::requestProfiles() {
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
        if (!active_ || request_generation != generation_) {
            return;
        }
        profiles_request_pending_ = false;
        const bool refresh_again = std::exchange(profiles_refresh_queued_, false);
        if (reply.isError()) {
            setLastError(tr("Could not load backup profiles from the system manager."), reply.error().name());
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
    auto* watcher = new QDBusPendingCallWatcher(
        btrfsbackup::kde::ManagerClient{bus_}.status(requested_profile),
        this
    );
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, request_generation, requested_profile](QDBusPendingCallWatcher*) {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        if (!active_ || request_generation != generation_ || requested_profile != profile_) {
            return;
        }
        status_request_pending_ = false;
        const bool refresh_again = std::exchange(status_refresh_queued_, false);
        if (reply.isError()) {
            setLastError(tr("Could not load backup status from the system manager."), reply.error().name());
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
        setLastError(tr("Invalid manager response."), QStringLiteral("manager.invalid-response"));
        return;
    }

    QString profile_name;
    bool profile_enabled = true;
    bool configuration_valid = true;
    QString configuration_error_code;
    QVariantList profiles;
    for (const auto& decoded : *decoded_profiles) {
        QVariantMap profile;
        profile.insert(QStringLiteral("profileId"), decoded.id);
        profile.insert(QStringLiteral("name"), decoded.name);
        profile.insert(QStringLiteral("enabled"), decoded.enabled);
        profile.insert(QStringLiteral("targetName"), decoded.target_name);
        profile.insert(QStringLiteral("configurationValid"), decoded.configuration_valid);
        profile.insert(QStringLiteral("configurationErrorCode"), decoded.configuration_error_code);
        profiles.push_back(profile);
        if (decoded.id == profile_) {
            profile_name = decoded.name;
            profile_enabled = decoded.enabled;
            configuration_valid = decoded.configuration_valid;
            configuration_error_code = decoded.configuration_error_code;
        }
    }
    if (profiles_ != profiles) {
        profiles_ = profiles;
        emit profilesChanged();
    }
    if (profile_name_ != profile_name || profile_enabled_ != profile_enabled ||
        configuration_valid_ != configuration_valid || configuration_error_code_ != configuration_error_code) {
        profile_name_ = profile_name;
        profile_enabled_ = profile_enabled;
        configuration_valid_ = configuration_valid;
        configuration_error_code_ = configuration_error_code;
        emit statusChanged();
    }
}

void BackupStatusModel::applyStatus(const QString& payload) {
    if (!run_.apply(payload)) {
        setLastError(tr("The backup manager returned an unsupported status schema."), QStringLiteral("manager.unsupported-status-schema"));
        return;
    }
    setManagerConnected(true);
    setLastError(QString());
}

void BackupStatusModel::applyDeviceState(const QString& payload) {
    if (!target_.apply(profile_, payload)) {
        setLastError(tr("The backup manager returned an unsupported target schema."), QStringLiteral("manager.unsupported-target-schema"));
        return;
    }
    setLastError(QString());
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
        {QLatin1String(btrfsbackup::manager_protocol::method::set_profile_enabled), QLatin1String(btrfsbackup::manager_protocol::feature::profile_activation)},
    };
    if (!supports(required_features.value(method))) {
        setLastError(tr("The backup manager does not support this operation."), QStringLiteral("manager.unsupported-operation"));
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
            setLastError(
                i18nd(
                    translation_domain,
                    "The requested backup operation failed: %1",
                    manager_error_message(reply.error())
                ),
                reply.error().name()
            );
            emit operationChanged();
            return;
        }
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(reply.value().toUtf8(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject() ||
            json_int(document.object(), "schemaVersion", -1) != 1) {
            setLastError(parseError(error), QStringLiteral("manager.invalid-operation-response"));
            emit operationChanged();
            return;
        }
        last_operation_ = document.object().value(QStringLiteral("operation")).toString();
        operation_message_timer_.start();
        emit operationChanged();
        if (shared_source_ != nullptr) {
            shared_source_->refreshNow();
        } else {
            requestProfiles();
            requestStatus();
            requestDeviceState();
            requestHistory();
        }
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

void BackupStatusModel::setLastError(const QString& message, const QString& code) {
    if (last_error_ == message && last_error_code_ == code) {
        return;
    }
    last_error_ = message;
    last_error_code_ = code;
    emit errorChanged();
}

void BackupStatusModel::managerUnavailable() {
    ++generation_;
    capabilities_verified_ = false;
    profiles_request_pending_ = false;
    status_request_pending_ = false;
    device_request_pending_ = false;
    profiles_refresh_queued_ = false;
    status_refresh_queued_ = false;
    device_refresh_queued_ = false;
    features_.clear();
    run_.setCancelSupported(false);
    target_.setStorageSupported(false);
    target_.reset();
    operation_pending_ = false;
    emit operationChanged();
    setManagerConnected(false);
    if (last_error_.isEmpty()) {
        setLastError(tr("The system backup manager is unavailable."), QStringLiteral("manager.unavailable"));
    }
}
