// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ProfileConfigurationModel.hpp"

#include <ManagerApi.hpp>
#include <core/ManagerProtocol.hpp>

#include <KLocalizedString>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>

namespace btrfsbackup::kde::kcm {

namespace {

QString compact_json(const QJsonObject& object) {
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

} // namespace

ProfileConfigurationModel::ProfileConfigurationModel(QObject* parent)
    : QObject(parent), bus_(QDBusConnection::systemBus()), manager_events_(bus_, this) {
    connect(&manager_events_, &btrfsbackup::kde::ManagerEventSubscriber::profilesChanged, this, [this]() {
        if (!loaded_)
            return;
        if (busy_) {
            refresh_pending_ = true;
        } else {
            loadDetails(profileId());
        }
    });
    connect(&manager_events_, &btrfsbackup::kde::ManagerEventSubscriber::deviceStateChanged, this, [this](const QString& profile_id) {
        if (!loaded_ || profile_id != profileId())
            return;
        if (busy_)
            refresh_pending_ = true;
        else
            loadDetails(profile_id);
    });
}

QString ProfileConfigurationModel::profileId() const {
    return profile_.value(QStringLiteral("profileId")).toString();
}
QString ProfileConfigurationModel::name() const {
    return profile_.value(QStringLiteral("name")).toString();
}
bool ProfileConfigurationModel::enabled() const {
    return profile_.value(QStringLiteral("enabled")).toBool();
}
QVariantMap ProfileConfigurationModel::target() const {
    return object("target").toVariantMap();
}
QVariantMap ProfileConfigurationModel::paths() const {
    return object("paths").toVariantMap();
}
QVariantMap ProfileConfigurationModel::settings() const {
    return object("settings").toVariantMap();
}
QVariantList ProfileConfigurationModel::sources() const {
    return profile_.value(QStringLiteral("sources")).toArray().toVariantList();
}
QVariantList ProfileConfigurationModel::sourceCandidates() const {
    return source_candidates_;
}
bool ProfileConfigurationModel::configurationValid() const {
    return configuration_valid_;
}
QString ProfileConfigurationModel::configurationErrorCode() const {
    return configuration_error_code_;
}
int ProfileConfigurationModel::schemaVersion() const {
    return profile_.value(QStringLiteral("schemaVersion")).toInt();
}
QString ProfileConfigurationModel::generation() const {
    return generation_;
}
QString ProfileConfigurationModel::fingerprint() const {
    return fingerprint_;
}
bool ProfileConfigurationModel::loaded() const {
    return loaded_;
}
bool ProfileConfigurationModel::busy() const {
    return busy_;
}
QString ProfileConfigurationModel::errorCode() const {
    return error_code_;
}
QString ProfileConfigurationModel::errorMessage() const {
    return error_message_;
}
QString ProfileConfigurationModel::operationMessage() const {
    return operation_message_;
}

QJsonObject ProfileConfigurationModel::object(const char* key) const {
    return profile_.value(QLatin1String(key)).toObject();
}

void ProfileConfigurationModel::load(const QString& profile_id) {
    loadDetails(profile_id);
}

void ProfileConfigurationModel::loadDetails(const QString& profile_id) {
    if (!busy_ && !profile_id.isEmpty())
        request(RequestKind::LoadDetails, QLatin1String(manager_protocol::method::get_profile_details), {profile_id});
}

void ProfileConfigurationModel::reload() {
    if (loaded_)
        loadDetails(profileId());
}

void ProfileConfigurationModel::clearError() {
    setError({}, {});
}

void ProfileConfigurationModel::addSourceConfiguration(
    const QString& name,
    const QString& subvolume,
    int local_retention,
    int remote_retention
) {
    const QString clean_subvolume = QDir::cleanPath(subvolume.trimmed());
    if (!loaded_ || busy_ || name.trimmed().isEmpty() || !QDir::isAbsolutePath(clean_subvolume))
        return;
    const QJsonObject payload{
        {QStringLiteral("name"), name.trimmed()},
        {QStringLiteral("subvolume"), clean_subvolume},
        {QStringLiteral("localRetention"), local_retention},
        {QStringLiteral("remoteRetention"), remote_retention},
    };
    request(RequestKind::AddSource, QLatin1String(manager_protocol::method::add_profile_source), {profileId(), generation_, fingerprint_, compact_json(payload)});
}

void ProfileConfigurationModel::updateSourceConfiguration(
    int index,
    const QString& name,
    int local_retention,
    int remote_retention
) {
    const QJsonArray source_array = profile_.value(QStringLiteral("sources")).toArray();
    if (!loaded_ || busy_ || index < 0 || index >= source_array.size() || name.trimmed().isEmpty())
        return;
    const QJsonObject payload{
        {QStringLiteral("name"), name.trimmed()},
        {QStringLiteral("localRetention"), local_retention},
        {QStringLiteral("remoteRetention"), remote_retention},
    };
    const QString source_id = source_array.at(index).toObject().value(QStringLiteral("id")).toString();
    request(RequestKind::UpdateSource, QLatin1String(manager_protocol::method::update_profile_source), {profileId(), source_id, generation_, fingerprint_, compact_json(payload)});
}

void ProfileConfigurationModel::removeSourceConfiguration(int index) {
    const QJsonArray source_array = profile_.value(QStringLiteral("sources")).toArray();
    if (!loaded_ || busy_ || index < 0 || index >= source_array.size() || source_array.size() <= 1)
        return;
    const QString source_id = source_array.at(index).toObject().value(QStringLiteral("id")).toString();
    request(RequestKind::RemoveSource, QLatin1String(manager_protocol::method::remove_profile_source), {profileId(), source_id, generation_, fingerprint_});
}

void ProfileConfigurationModel::updateProfileSettings(const QString& name, bool daily_limit, bool auto_eject) {
    if (!loaded_ || busy_ || name.trimmed().isEmpty())
        return;
    const QJsonObject payload{
        {QStringLiteral("name"), name.trimmed()},
        {QStringLiteral("dailyLimit"), daily_limit},
        {QStringLiteral("autoEject"), auto_eject},
    };
    request(RequestKind::UpdateSettings, QLatin1String(manager_protocol::method::update_profile_settings), {profileId(), generation_, fingerprint_, compact_json(payload)});
}

void ProfileConfigurationModel::deleteProfile() {
    if (loaded_ && !busy_)
        request(RequestKind::Delete, QLatin1String(manager_protocol::method::delete_profile), {profileId(), generation_, fingerprint_});
}

void ProfileConfigurationModel::request(RequestKind kind, const QString& method, const QVariantList& arguments) {
    operation_message_.clear();
    setBusy(true);
    setError({}, {});
    auto* watcher = new QDBusPendingCallWatcher(manager_call(bus_, method, arguments), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, kind](QDBusPendingCallWatcher*) {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        if (reply.isError()) {
            if (kind == RequestKind::LoadDetails && reply.error().name().endsWith(QStringLiteral(".NotFound"))) {
                const QString deleted_id = profileId();
                profile_ = {};
                generation_.clear();
                fingerprint_.clear();
                loaded_ = false;
                refresh_pending_ = false;
                emit profileChanged();
                setBusy(false);
                emit profileDeleted(deleted_id);
                return;
            }
            setError(reply.error().name(), reply.error().message());
            setBusy(false);
            if (reply.error().name().endsWith(QStringLiteral(".Conflict")))
                emit conflictDetected();
            if (refresh_pending_) {
                refresh_pending_ = false;
                loadDetails(profileId());
            }
            return;
        }
        if (kind == RequestKind::Delete) {
            const QString deleted_id = profileId();
            profile_ = {};
            generation_.clear();
            fingerprint_.clear();
            loaded_ = false;
            refresh_pending_ = false;
            operation_message_ = i18n("Profile deleted");
            emit profileChanged();
            setBusy(false);
            emit profileDeleted(deleted_id);
            return;
        }
        if (!applyEnvelope(reply.value())) {
            setError(QStringLiteral("manager.invalid-response"), i18n("The backup manager returned an invalid profile response."));
            setBusy(false);
            return;
        }
        setBusy(false);
        if (kind != RequestKind::LoadDetails) {
            operation_message_ = i18n("Profile saved");
            emit profileSaved(profileId());
        }
        emit stateChanged();
        if (refresh_pending_) {
            refresh_pending_ = false;
            loadDetails(profileId());
        }
    });
}

bool ProfileConfigurationModel::applyEnvelope(const QString& payload) {
    QJsonParseError error;
    const QJsonDocument response = QJsonDocument::fromJson(payload.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !response.isObject())
        return false;
    const QJsonObject envelope = response.object();
    if (envelope.value(QStringLiteral("schemaVersion")).toInt() != manager_protocol::profile_details_schema_version ||
        !envelope.value(QStringLiteral("document")).isObject())
        return false;
    profile_ = envelope.value(QStringLiteral("document")).toObject();
    generation_ = envelope.value(QStringLiteral("generation")).toString();
    fingerprint_ = envelope.value(QStringLiteral("fingerprint")).toString();
    source_candidates_ = envelope.value(QStringLiteral("sourceCandidates")).toArray().toVariantList();
    configuration_valid_ = envelope.value(QStringLiteral("configurationValid")).toBool(true);
    configuration_error_code_ = envelope.value(QStringLiteral("configurationErrorCode")).toString();
    loaded_ = true;
    emit profileChanged();
    return true;
}

void ProfileConfigurationModel::setError(const QString& code, const QString& message) {
    if (error_code_ == code && error_message_ == message)
        return;
    error_code_ = code;
    error_message_ = message;
    emit stateChanged();
}

void ProfileConfigurationModel::setBusy(bool value) {
    if (busy_ == value)
        return;
    busy_ = value;
    emit stateChanged();
}

} // namespace btrfsbackup::kde::kcm
