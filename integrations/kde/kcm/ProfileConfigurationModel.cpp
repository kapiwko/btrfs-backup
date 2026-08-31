// SPDX-FileCopyrightText: 2026 Kamil Piwowarski <kapiwko@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ProfileConfigurationModel.hpp"

#include <ManagerApi.hpp>
#include <core/ManagerProtocol.hpp>

#include <QDBusError>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <KLocalizedString>

namespace btrfsbackup::kde::kcm {

ProfileConfigurationModel::ProfileConfigurationModel(QObject* parent)
    : QObject(parent), bus_(QDBusConnection::systemBus()) {
}

QString ProfileConfigurationModel::profileId() const {
    return draft_.value(QStringLiteral("profileId")).toString();
}
QString ProfileConfigurationModel::name() const {
    return draft_.value(QStringLiteral("name")).toString();
}
bool ProfileConfigurationModel::enabled() const {
    return draft_.value(QStringLiteral("enabled")).toBool();
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
    return draft_.value(QStringLiteral("sources")).toArray().toVariantList();
}
int ProfileConfigurationModel::schemaVersion() const {
    return draft_.value(QStringLiteral("schemaVersion")).toInt();
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
bool ProfileConfigurationModel::newDraft() const {
    return new_draft_;
}
bool ProfileConfigurationModel::dirty() const {
    return dirty_;
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
QString ProfileConfigurationModel::validationPreview() const {
    return validation_preview_;
}

void ProfileConfigurationModel::load(const QString& profile_id) {
    loadDetails(profile_id);
}

void ProfileConfigurationModel::loadDetails(const QString& profile_id) {
    if (busy_ || profile_id.isEmpty())
        return;
    request(RequestKind::LoadDetails, QLatin1String(manager_protocol::method::get_profile_details), {profile_id});
}

void ProfileConfigurationModel::loadForEditing(const QString& profile_id) {
    if (busy_ || profile_id.isEmpty())
        return;
    request(RequestKind::LoadEditing, QLatin1String(manager_protocol::method::get_profile_for_editing), {profile_id});
}

void ProfileConfigurationModel::createDraft(const QString& profile_id) {
    if (busy_ || profile_id.isEmpty())
        return;
    const QString remote_root = QStringLiteral("/mnt/btrfs-backup/%1").arg(profile_id);
    draft_ = QJsonObject{
        {QStringLiteral("schemaVersion"), 4},
        {QStringLiteral("profileId"), profile_id},
        {QStringLiteral("name"), profile_id},
        {QStringLiteral("enabled"), true},
        {QStringLiteral("target"), QJsonObject{
                                       {QStringLiteral("device"), QString()},
                                       {QStringLiteral("luksUuid"), QString()},
                                       {QStringLiteral("btrfsUuid"), QString()},
                                       {QStringLiteral("partitionUuid"), QString()},
                                       {QStringLiteral("serial"), QString()},
                                       {QStringLiteral("mapperName"), QStringLiteral("backupdisk")},
                                       {QStringLiteral("activation"), QJsonObject{{QStringLiteral("mode"), QStringLiteral("askPassword")}}},
                                   }},
        {QStringLiteral("paths"), QJsonObject{
                                      {QStringLiteral("remoteRoot"), remote_root + QStringLiteral("/snapshots")},
                                      {QStringLiteral("incomingRoot"), remote_root + QStringLiteral("/.incoming")},
                                  }},
        {QStringLiteral("settings"), QJsonObject{
                                         {QStringLiteral("dailyLimit"), true},
                                         {QStringLiteral("incrementalRequired"), true},
                                         {QStringLiteral("keepFailedLocalSnapshot"), false},
                                         {QStringLiteral("autoEject"), true},
                                         {QStringLiteral("remoteRetention"), 30},
                                         {QStringLiteral("localRetention"), 30},
                                         {QStringLiteral("minimumTargetFreeBytes"), 5368709120LL},
                                         {QStringLiteral("minimumLocalFreeBytes"), 1073741824LL},
                                     }},
        {QStringLiteral("sources"), QJsonArray{}},
    };
    baseline_ = draft_;
    generation_.clear();
    fingerprint_.clear();
    validation_preview_.clear();
    error_code_.clear();
    error_message_.clear();
    operation_message_.clear();
    loaded_ = true;
    new_draft_ = true;
    updateDirty();
    emit draftChanged();
    emit stateChanged();
}

void ProfileConfigurationModel::reload() {
    if (loaded_)
        loadDetails(profileId());
}

void ProfileConfigurationModel::discard() {
    if (!loaded_)
        return;
    draft_ = baseline_;
    validation_preview_.clear();
    updateDirty();
    emit draftChanged();
}

void ProfileConfigurationModel::setName(const QString& value) {
    if (!loaded_ || name() == value)
        return;
    draft_.insert(QStringLiteral("name"), value);
    validation_preview_.clear();
    updateDirty();
    emit draftChanged();
}

void ProfileConfigurationModel::setEnabled(bool value) {
    if (!loaded_ || enabled() == value)
        return;
    draft_.insert(QStringLiteral("enabled"), value);
    validation_preview_.clear();
    updateDirty();
    emit draftChanged();
}

QJsonObject ProfileConfigurationModel::object(const char* key) const {
    return draft_.value(QLatin1String(key)).toObject();
}

void ProfileConfigurationModel::setObject(const char* key, const QJsonObject& value) {
    if (object(key) == value)
        return;
    draft_.insert(QLatin1String(key), value);
    validation_preview_.clear();
    updateDirty();
    emit draftChanged();
}

void ProfileConfigurationModel::setTargetValue(const QString& key, const QVariant& value) {
    if (!loaded_)
        return;
    QJsonObject target_object = object("target");
    if (key.startsWith(QStringLiteral("activation."))) {
        QJsonObject activation = target_object.value(QStringLiteral("activation")).toObject();
        const QString activation_key = key.sliced(QStringLiteral("activation.").size());
        activation.insert(activation_key, QJsonValue::fromVariant(value));
        if (activation_key == QStringLiteral("mode") && value.toString() == QStringLiteral("askPassword"))
            activation.remove(QStringLiteral("keyFile"));
        target_object.insert(QStringLiteral("activation"), activation);
    } else {
        target_object.insert(key, QJsonValue::fromVariant(value));
    }
    setObject("target", target_object);
}

void ProfileConfigurationModel::setSettingValue(const QString& key, const QVariant& value) {
    if (!loaded_)
        return;
    QJsonObject settings_object = object("settings");
    settings_object.insert(key, QJsonValue::fromVariant(value));
    setObject("settings", settings_object);
}

void ProfileConfigurationModel::setSourceValue(int index, const QString& key, const QVariant& value) {
    QJsonArray source_array = draft_.value(QStringLiteral("sources")).toArray();
    if (!loaded_ || index < 0 || index >= source_array.size())
        return;
    QJsonObject source = source_array.at(index).toObject();
    const QJsonValue json_value = QJsonValue::fromVariant(value);
    if (source.value(key) == json_value)
        return;
    source.insert(key, json_value);
    source_array.replace(index, source);
    draft_.insert(QStringLiteral("sources"), source_array);
    validation_preview_.clear();
    updateDirty();
    emit draftChanged();
}

void ProfileConfigurationModel::addSource() {
    if (!loaded_)
        return;
    QJsonArray source_array = draft_.value(QStringLiteral("sources")).toArray();
    const int number = source_array.size() + 1;
    source_array.append(QJsonObject{
        {QStringLiteral("id"), QStringLiteral("source%1").arg(number)},
        {QStringLiteral("name"), i18n("Source %1", number)},
        {QStringLiteral("enabled"), true},
        {QStringLiteral("subvolume"), QStringLiteral("/")},
        {QStringLiteral("localSnapshotDir"), QStringLiteral("/.snapshots/source%1").arg(number)},
        {QStringLiteral("remoteSubdir"), QStringLiteral("source%1").arg(number)},
        {QStringLiteral("remoteRetention"), 30},
        {QStringLiteral("localRetention"), 30},
    });
    draft_.insert(QStringLiteral("sources"), source_array);
    validation_preview_.clear();
    updateDirty();
    emit draftChanged();
}

void ProfileConfigurationModel::removeSource(int index) {
    QJsonArray source_array = draft_.value(QStringLiteral("sources")).toArray();
    if (!loaded_ || index < 0 || index >= source_array.size())
        return;
    source_array.removeAt(index);
    draft_.insert(QStringLiteral("sources"), source_array);
    validation_preview_.clear();
    updateDirty();
    emit draftChanged();
}

void ProfileConfigurationModel::addSourceConfiguration(
    const QString& name,
    const QString& subvolume,
    int local_retention,
    int remote_retention
) {
    const QString trimmed_name = name.trimmed();
    const QString clean_subvolume = QDir::cleanPath(subvolume.trimmed());
    if (!loaded_ || busy_ || trimmed_name.isEmpty() || !QDir::isAbsolutePath(clean_subvolume) ||
        local_retention < 1 || local_retention > 100000 ||
        remote_retention < 1 || remote_retention > 100000)
        return;

    QJsonArray source_array = draft_.value(QStringLiteral("sources")).toArray();
    const QString source_id = nextSourceId(trimmed_name, clean_subvolume);
    const QString snapshot_root = QDir(QFileInfo(clean_subvolume).absolutePath()).filePath(QStringLiteral(".snapshots/btrfs-backup/%1").arg(source_id));
    source_array.append(QJsonObject{
        {QStringLiteral("id"), source_id},
        {QStringLiteral("name"), trimmed_name},
        {QStringLiteral("enabled"), true},
        {QStringLiteral("subvolume"), clean_subvolume},
        {QStringLiteral("localSnapshotDir"), QDir::cleanPath(snapshot_root)},
        {QStringLiteral("remoteSubdir"), source_id},
        {QStringLiteral("remoteRetention"), remote_retention},
        {QStringLiteral("localRetention"), local_retention},
    });
    draft_.insert(QStringLiteral("sources"), source_array);
    validation_preview_.clear();
    updateDirty();
    emit draftChanged();
    save();
}

void ProfileConfigurationModel::updateSourceConfiguration(
    int index,
    const QString& name,
    int local_retention,
    int remote_retention
) {
    QJsonArray source_array = draft_.value(QStringLiteral("sources")).toArray();
    const QString trimmed_name = name.trimmed();
    if (!loaded_ || busy_ || index < 0 || index >= source_array.size() || trimmed_name.isEmpty() ||
        local_retention < 1 || local_retention > 100000 ||
        remote_retention < 1 || remote_retention > 100000)
        return;

    QJsonObject source = source_array.at(index).toObject();
    source.insert(QStringLiteral("name"), trimmed_name);
    source.insert(QStringLiteral("localRetention"), local_retention);
    source.insert(QStringLiteral("remoteRetention"), remote_retention);
    source_array.replace(index, source);
    draft_.insert(QStringLiteral("sources"), source_array);
    validation_preview_.clear();
    updateDirty();
    emit draftChanged();
    save();
}

void ProfileConfigurationModel::removeSourceConfiguration(int index) {
    const QJsonArray source_array = draft_.value(QStringLiteral("sources")).toArray();
    if (!loaded_ || busy_ || index < 0 || index >= source_array.size() || source_array.size() <= 1)
        return;
    removeSource(index);
    save();
}

void ProfileConfigurationModel::updateProfileSettings(
    const QString& name,
    bool daily_limit,
    bool auto_eject
) {
    const QString trimmed_name = name.trimmed();
    if (!loaded_ || busy_ || trimmed_name.isEmpty())
        return;

    QJsonObject settings_object = object("settings");
    settings_object.insert(QStringLiteral("dailyLimit"), daily_limit);
    settings_object.insert(QStringLiteral("autoEject"), auto_eject);
    draft_.insert(QStringLiteral("name"), trimmed_name);
    draft_.insert(QStringLiteral("settings"), settings_object);
    validation_preview_.clear();
    updateDirty();
    emit draftChanged();
    save();
}

QString ProfileConfigurationModel::nextSourceId(const QString& name, const QString& subvolume) const {
    QString candidate = name.toLower();
    if (candidate.isEmpty())
        candidate = QFileInfo(subvolume).fileName().toLower();
    candidate.replace(QRegularExpression(QStringLiteral("[^a-z0-9._-]+")), QStringLiteral("-"));
    candidate.remove(QRegularExpression(QStringLiteral("^[^a-z0-9]+|[^a-z0-9]+$")));
    if (candidate.isEmpty())
        candidate = QStringLiteral("source");
    candidate = candidate.left(48);

    QSet<QString> existing;
    for (const QJsonValue& value : draft_.value(QStringLiteral("sources")).toArray())
        existing.insert(value.toObject().value(QStringLiteral("id")).toString());
    if (!existing.contains(candidate))
        return candidate;
    for (int suffix = 2; suffix < 10000; ++suffix) {
        const QString numbered = QStringLiteral("%1-%2").arg(candidate.left(58), QString::number(suffix));
        if (!existing.contains(numbered))
            return numbered;
    }
    return QStringLiteral("source-%1").arg(existing.size() + 1);
}

void ProfileConfigurationModel::validate() {
    if (!loaded_ || busy_)
        return;
    request(RequestKind::Validate, QLatin1String(manager_protocol::method::validate_profile_draft), {profileId(), generation_, fingerprint_, QString::fromUtf8(QJsonDocument(draft_).toJson(QJsonDocument::Compact))});
}

void ProfileConfigurationModel::save() {
    if (!loaded_ || !dirty_ || busy_)
        return;
    request(RequestKind::Save, QLatin1String(manager_protocol::method::save_profile), {profileId(), generation_, fingerprint_, QString::fromUtf8(QJsonDocument(draft_).toJson(QJsonDocument::Compact))});
}

void ProfileConfigurationModel::duplicateAs(const QString& new_profile_id) {
    if (!loaded_ || busy_ || new_profile_id.isEmpty())
        return;
    QJsonObject duplicate = draft_;
    duplicate.insert(QStringLiteral("profileId"), new_profile_id);
    duplicate.remove(QStringLiteral("configurationGeneration"));
    request(RequestKind::Duplicate, QLatin1String(manager_protocol::method::save_profile_hooks), {new_profile_id, QString(), QString(), QString::fromUtf8(QJsonDocument(duplicate).toJson(QJsonDocument::Compact))});
}

void ProfileConfigurationModel::deleteProfile() {
    if (!loaded_ || busy_)
        return;
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
            setError(reply.error().name(), reply.error().message());
            setBusy(false);
            if (reply.error().name().endsWith(QStringLiteral(".Conflict")))
                emit conflictDetected();
            return;
        }
        if (kind == RequestKind::Delete) {
            const QString deleted_id = profileId();
            draft_ = {};
            baseline_ = {};
            generation_.clear();
            fingerprint_.clear();
            loaded_ = false;
            new_draft_ = false;
            dirty_ = false;
            operation_message_ = i18n("Profile deleted");
            emit draftChanged();
            setBusy(false);
            emit stateChanged();
            emit profileDeleted(deleted_id);
            return;
        }
        const bool replace = kind == RequestKind::LoadDetails || kind == RequestKind::LoadEditing ||
            kind == RequestKind::Save;
        if (!applyEnvelope(reply.value(), replace)) {
            setError(QStringLiteral("manager.invalid-response"), i18n("The backup manager returned an invalid editing response."));
            setBusy(false);
            return;
        }
        setBusy(false);
        if (kind == RequestKind::Validate)
            operation_message_ = i18n("Profile draft is valid");
        if (kind == RequestKind::Save) {
            operation_message_ = i18n("Profile saved");
            emit profileSaved(profileId());
        }
        if (kind == RequestKind::Duplicate) {
            QJsonParseError error;
            const auto response = QJsonDocument::fromJson(reply.value().toUtf8(), &error).object();
            operation_message_ = i18n("Profile duplicated");
            emit profileSaved(response.value(QStringLiteral("profileId")).toString());
        }
        emit stateChanged();
    });
}

bool ProfileConfigurationModel::applyEnvelope(const QString& payload, bool replace_draft) {
    QJsonParseError error;
    const QJsonDocument response = QJsonDocument::fromJson(payload.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !response.isObject())
        return false;
    const QJsonObject envelope = response.object();
    if (envelope.value(QStringLiteral("schemaVersion")).toInt() != manager_protocol::profile_edit_schema_version ||
        !envelope.value(QStringLiteral("document")).isObject())
        return false;
    if (replace_draft) {
        draft_ = envelope.value(QStringLiteral("document")).toObject();
        baseline_ = draft_;
        new_draft_ = false;
        generation_ = envelope.value(QStringLiteral("generation")).toString();
        fingerprint_ = envelope.value(QStringLiteral("fingerprint")).toString();
        loaded_ = true;
        updateDirty();
        emit draftChanged();
    } else {
        validation_preview_ = QString::fromUtf8(
            QJsonDocument(envelope.value(QStringLiteral("document")).toObject()).toJson(QJsonDocument::Indented)
        );
    }
    return true;
}

void ProfileConfigurationModel::updateDirty() {
    const bool value = loaded_ && (new_draft_ || draft_ != baseline_);
    if (dirty_ == value)
        return;
    dirty_ = value;
    emit stateChanged();
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
